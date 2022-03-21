//
// Copyright (c) 2017, Nicolas Burrus
// This software may be modified and distributed under the terms
// of the BSD license.  See the LICENSE file for details.
//

#include "Server.h"

#include <libzv/Utils.h>
#include <libzv/ImageList.h>

#include <client/kissnet_zv.h>
#include <client/Message.h>

#include <deque>
#include <thread>
#include <condition_variable>
#include <iostream>

namespace kn = kissnet;

namespace zv
{

struct ImageContext
{
    std::mutex lock;
    uint64_t clientImageId = 0;
    void *clientSocket = nullptr;
    ImageSRGBAPtr maybeLoadedImage;
};
using ImageContextPtr = std::shared_ptr<ImageContext>;

struct NetworkImageItemData::Impl
{
    ImageContextPtr ctx;
};

NetworkImageItemData::NetworkImageItemData ()
: impl (new Impl())
{}

NetworkImageItemData::~NetworkImageItemData () = default;

bool NetworkImageItemData::update ()
{
    if (cpuData->hasData())
        return false;

    std::lock_guard<std::mutex> _ (impl->ctx->lock);
    if (impl->ctx->maybeLoadedImage)
    {        
        cpuData.swap (impl->ctx->maybeLoadedImage);
        status = Status::Ready;
        return true;
    }
    else
    {
        // Need to wait some more, still no data available.
        return false;
    }
}

struct ServerPayloadReader : PayloadReader
{
    ServerPayloadReader(const std::vector<uint8_t>& payload) : PayloadReader (payload) {}

    void readImageBuffer (ImageSRGBA& image)
    {
        int w = readUInt32 ();
        int h = readUInt32 ();
        int sourceBytesPerRow = readUInt32 ();
        image.ensureAllocatedBufferForSize (w, h);
        for (int r = 0; r < h; ++r)
        {
            const size_t rowContentSizeInBytes = w*sizeof(PixelSRGBA);
            readBytes(reinterpret_cast<uint8_t*>(image.atRowPtr(r)), rowContentSizeInBytes);
            skipBytes (sourceBytesPerRow - rowContentSizeInBytes);
        }
    }
};

struct ServerWriterThread
{
    ServerWriterThread ()
    {}

    ~ServerWriterThread ()
    {
        stop();
    }

    void start (kn::tcp_socket* socket)
    {
        _socket = socket;
        _writeThread = std::thread([this]() {
            run ();
        });
    }

    void stop ()
    {
        _messageAdded.notify_all();
        _shouldDisconnect = true;
        if (_writeThread.joinable())
            _writeThread.join();
    }

    void enqueueMessage (Message&& msg)
    {
        std::lock_guard<std::mutex> _(_outputQueueMutex);
        _outputQueue.push_back (std::move(msg));
        _messageAdded.notify_all();
    }

private:
    void run ()
    {
        while (!_shouldDisconnect)
        {
            std::unique_lock<std::mutex> lk (_outputQueueMutex);
            _messageAdded.wait (lk, [this]() {
                return _shouldDisconnect || !_outputQueue.empty();
            });
            // Mutex locked again.

            std::clog << "[DEBUG][WRITER] Got event, checking if anything to send." << std::endl;
            std::deque<Message> messagesToSend;
            messagesToSend.swap(_outputQueue);
            lk.unlock ();

            while (!messagesToSend.empty())
            {
                try
                {
                    sendMessage(*_socket, std::move(messagesToSend.front()));
                    messagesToSend.pop_front();
                }
                catch (const std::exception& e)
                {
                    std::clog << "Got an exception, stopping the connection: " << e.what() << std::endl;
                    _shouldDisconnect = true;
                    break;
                }
            }
        }
    }

private:
    kn::tcp_socket* _socket = nullptr;
    bool _shouldDisconnect = false;
    std::thread _writeThread;
    std::condition_variable _messageAdded;
    std::mutex _outputQueueMutex;
    std::deque<Message> _outputQueue;
};

struct ServerThread
{
    void start (const std::string& hostname, int port)
    {
        _thread = std::thread([this, hostname, port]() {
            run (hostname, port);
        });
    }

    void run (const std::string& hostname, int port)
    {
        try
        {
            kn::tcp_socket server(kn::endpoint(hostname, port));
            server.bind();
            server.listen();

            _clientSocket = std::make_unique<kn::tcp_socket>(server.accept());
            while (!_shouldDisconnect)
            {
                Message msg = recvMessage ();
                switch (msg.kind)
                {
                case MessageKind::Close: {
                    _shouldDisconnect = true;
                    break;
                }                

                case MessageKind::Image: {
                    // uniqueId:uint64_t name:StringUTF8 flags:uint32_t imageBuffer:ImageBuffer
                    ImageItemUniquePtr imageItem = std::make_unique<ImageItem>();
                    // imageItem->uniqueId will be set later, once transmistted to the main thread.
                    ServerPayloadReader reader (msg.payload);
                    const uint64_t clientImageId = reader.readUInt64();
                    reader.readStringUTF8(imageItem->prettyName);
                    const uint32_t flags = reader.readUInt32();
                    
                    imageItem->source = ImageItem::Source::Network;
                    ImageSRGBA imageContent;
                    reader.readImageBuffer (imageContent);
                    if (imageContent.hasData())
                    {
                        imageItem->sourceData = std::make_shared<ImageSRGBA>(std::move(imageContent));
                    }
                    else
                    {
                        ImageContextPtr ctx = std::make_shared<ImageContext>();
                        ctx->clientImageId = clientImageId;
                        ctx->clientSocket = _clientSocket.get();
                        _availableImages[clientImageId] = ctx;
                        imageItem->loadDataCallback = [this, ctx]()
                        {
                            return this->onLoadData(ctx);
                        };
                    }

                    {
                        std::lock_guard<std::mutex> lk (_incomingImagesMutex);
                        _incomingImages.push_back(std::move(imageItem));
                    }
                    break;
                }

                case MessageKind::ImageBuffer: {
                    // uniqueId:uint64_t imageBuffer:ImageBuffer
                    ServerPayloadReader reader (msg.payload);
                    uint64_t clientImageId = reader.readUInt64 ();
                    auto it = _availableImages.find (clientImageId);
                    if (it == _availableImages.end())
                    {
                        zv_assert (false, "Unknown client image id!");
                        break;
                    }

                    const ImageContextPtr& ctx = it->second;
                    {
                        std::lock_guard<std::mutex> _(ctx->lock);
                        ctx->maybeLoadedImage = std::make_shared<ImageSRGBA>();
                        reader.readImageBuffer(*ctx->maybeLoadedImage);
                        zv_assert (ctx->clientSocket == _clientSocket.get(), "Client socket changed!");
                    }
                    break;
                }
                }
            }
        }
        catch (std::exception &e)
        {
            std::cerr << "Server got exception: " << e.what() << std::endl;
        }
        
        if (_clientSocket)
        {
            _clientSocket->close();
            _clientSocket.reset();
        }
    }

private:
    ImageItemDataUniquePtr onLoadData (const ImageContextPtr& ctx)
    {
        auto dataPtr = std::make_unique<NetworkImageItemData>();
        dataPtr->status = ImageItemData::Status::StillLoading;
        Message msg = requestImageBufferMessage (ctx->clientImageId);
        _writerThread.enqueueMessage (std::move(msg));
        return dataPtr;
    }

    static Message requestImageBufferMessage(uint64_t imageIdInClient)
    {
        Message msg;
        msg.kind = MessageKind::RequestImageBuffer;
        msg.payloadSizeInBytes = sizeof(uint64_t);
        msg.payload.reserve(msg.payloadSizeInBytes);
        PayloadWriter w(msg.payload);
        w.appendUInt64(imageIdInClient);
        assert(msg.payload.size() == msg.payloadSizeInBytes);
        return msg;
    }

    Message recvMessage ()
    {
        KnReader r (*_clientSocket);
        Message msg;
        msg.kind = (MessageKind)r.recvUInt32 ();
        msg.payloadSizeInBytes = r.recvUInt64 ();
        msg.payload.resize (msg.payloadSizeInBytes);
        if (msg.payloadSizeInBytes > 0)
            r.recvAllBytes (msg.payload.data(), msg.payloadSizeInBytes);
        return msg;
    }

    std::thread _thread;
    bool _shouldDisconnect = false;
    std::unique_ptr<kn::tcp_socket> _clientSocket;

    ServerWriterThread _writerThread;

    std::mutex _incomingImagesMutex;
    std::deque<ImageItemUniquePtr> _incomingImages;
    
    // <ImageID in client, ImageItemData>
    std::unordered_map<uint64_t, ImageContextPtr> _availableImages;
};

struct Server::Impl
{
    Impl()
    {}

    ServerThread _serverThread;
};

Server::Server()
: impl (new Impl())
{
}

Server::~Server() = default;

void Server::start (const std::string& hostname, int port)
{
    impl->_serverThread.start (hostname, port);    
}

void Server::stop ()
{

}

void Server::updateOnce ()
{

}

void Server::setImageReceivedCallback(const ImageReceivedCallback &callback)
{
    
}

} // zv
