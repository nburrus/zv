//
// Copyright (c) 2017, Nicolas Burrus
// This software may be modified and distributed under the terms
// of the BSD license.  See the LICENSE file for details.
//

#include <client/znet_zv.h>
#include <client/Message.h>

#include "Server.h"

#include <libzv/Utils.h>
#include <libzv/ImageList.h>

#include <stb_image.h>

#include <set>
#include <deque>
#include <thread>
#include <condition_variable>
#include <iostream>

namespace zn = zsummer::network;

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
    if (cpuData->hasData() || status != Status::StillLoading)
        return false;

    std::lock_guard<std::mutex> _ (impl->ctx->lock);
    if (impl->ctx->maybeLoadedImage)
    {        
        cpuData.swap (impl->ctx->maybeLoadedImage);
        // Make sure that if we need to request the image again
        // we won't think that it exists.
        impl->ctx->maybeLoadedImage.reset ();
        status = cpuData->hasData() ? Status::Ready : Status::FailedToLoad;
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

    void readImageBuffer (ImageSRGBA& image, std::string& filePath)
    {
        ImageBufferFormat format = (ImageBufferFormat)readUInt32 ();
        readStringUTF8 (filePath);
        int w = readUInt32 ();
        int h = readUInt32 ();
        int sourceBytesPerRow = readUInt32 ();

        switch (format)
        {
            case ImageBufferFormat::Raw_File:
            {
                zv_assert (h == 1 && w == 0, "Expected raw file content (w=%d h=%d)", w, h);
                std::vector<uint8_t> rawContent (sourceBytesPerRow);
                readBytes (rawContent.data(), sourceBytesPerRow);                
                int channels; // can return anything, but we requested 4, so the output data will have 4.
                uint8_t* imageContent = stbi_load_from_memory(rawContent.data(), rawContent.size(), &w, &h, &channels, 4);
                zv_assert (imageContent, "Failed to decode the image");
                image.ensureAllocatedBufferForSize (w, h);
                image.copyDataFrom (imageContent, 4*w, w, h);
                break;
            }

            case ImageBufferFormat::Data_RGBA32:
            {
                image.ensureAllocatedBufferForSize (w, h);
                for (int r = 0; r < h; ++r)
                {
                    const size_t rowContentSizeInBytes = w * sizeof(PixelSRGBA);
                    readBytes(reinterpret_cast<uint8_t *>(image.atRowPtr(r)), rowContentSizeInBytes);
                    skipBytes(sourceBytesPerRow - rowContentSizeInBytes);
                }
                break;
            }

            case ImageBufferFormat::Empty:
            {
                break;
            }

            default:
                zv_assert (false, "Invalid format %d", (uint32_t)format);
                break;
        }
    }
};

class ClientHandler
{    
public:
    using DisconnectCallback = std::function<void(void)>;

public:
    ~ClientHandler()
    {
        stop ();
    }

    struct IncomingImage
    {
        ImageItemUniquePtr item;
        uint32_t flags;
    };

    bool isConnected ()
    {
        return _socket != nullptr;
    }

    void start (const zn::EventLoopPtr& eventLoop, 
                const zn::TcpSocketPtr& socket,
                DisconnectCallback&& onDisconnectCb)
    {
        _eventLoop = eventLoop;
        _socket = socket;
        _onDisconnectCb = std::move(onDisconnectCb);

        _receiver = std::make_shared<zn::MessageReceiver>(_socket);
        _senderQueue = std::make_shared<zn::MessageSenderQueue>(_eventLoop, _socket, [this](zn::NetErrorCode err) {
            if (err != zn::NEC_SUCCESS)
                disconnect ();
        });

        recvMessage ();
        _senderQueue->enqueueMessage (versionMessage(1));
    }

    // Meant to be called by the manager.
    void stop ()
    {
        // Don't want to tell the guy calling you that you disconnected..
        _onDisconnectCb = nullptr;
        disconnect ();
    }

    // These are meant to be called from the main thread.
    void updateMainThead (const Server::ImageReceivedCallback& imageReceivedCallback)
    {
        std::lock_guard<std::mutex> lk (_incomingImagesMutex);
        while (!_incomingImages.empty())
        {
            IncomingImage im = std::move(_incomingImages.front());
            _incomingImages.pop_front();
            if (imageReceivedCallback)
            {
                im.item->uniqueId = UniqueId::newId();
                imageReceivedCallback (std::move(im.item), im.flags /* flags */);
            }
        }
    }

private:
    void recvMessage ()
    {
        if (!_receiver)
            return;
        _receiver->recvMessage([this](zn::NetErrorCode err, const Message &msg) { onMessage(err, msg); });
    }

    void disconnect ()
    {
        if (!_socket)
            return;
        _receiver.reset ();
        _senderQueue.reset ();
        _socket->doClose ();
        _socket.reset ();

        if (_onDisconnectCb)
        {
            DisconnectCallback cb = std::move(_onDisconnectCb);
            _onDisconnectCb = nullptr;
            cb();
        }
    }

    void onMessage (zn::NetErrorCode err, const zv::Message& msg)
    {
        if (err != zn::NEC_SUCCESS)
        {
            disconnect ();
            return;
        }

        // zv_dbg("Received message kind=%d", (int)msg.header.kind);
        switch (msg.header.kind)
        {
        case MessageKind::Image:
        {
            // uniqueId:uint64_t name:StringUTF8 flags:uint32_t imageBuffer:ImageBuffer
            ImageItemUniquePtr imageItem = std::make_unique<ImageItem>();
            // imageItem->uniqueId will be set later, once transmistted to the main thread.
            ServerPayloadReader reader(msg.payload);
            const uint64_t clientImageId = reader.readUInt64();
            reader.readStringUTF8(imageItem->prettyName);
            reader.readStringUTF8(imageItem->viewerName);
            const uint32_t flags = reader.readUInt32();

            ImageSRGBA imageContent;
            std::string imagePath;
            reader.readImageBuffer(imageContent, imagePath);
            imageItem->sourceImagePath = imagePath;
            if (imageContent.hasData())
            {
                imageItem->source = ImageItem::Source::Data;                
                imageItem->sourceData = std::make_shared<ImageSRGBA>(std::move(imageContent));
                imageItem->metadata.width = imageContent.width();
                imageItem->metadata.height = imageContent.height();
            }
            else
            {
                ImageContextPtr ctx = std::make_shared<ImageContext>();
                ctx->clientImageId = clientImageId;
                ctx->clientSocket = _socket.get();
                _availableImages[clientImageId] = ctx;
                imageItem->source = ImageItem::Source::Callback;
                imageItem->loadDataCallback = [this, ctx]()
                {
                    return this->onLoadData(ctx);
                };
            }

            {
                std::lock_guard<std::mutex> lk(_incomingImagesMutex);
                _incomingImages.push_back(IncomingImage{std::move(imageItem), flags});
            }
            break;
        }

        case MessageKind::ImageBuffer:
        {
            // uniqueId:uint64_t imageBuffer:ImageBuffer
            ServerPayloadReader reader(msg.payload);
            uint64_t clientImageId = reader.readUInt64();
            auto it = _availableImages.find(clientImageId);
            if (it == _availableImages.end())
            {
                zv_assert(false, "Unknown client image id!");
                break;
            }

            const ImageContextPtr &ctx = it->second;
            {
                std::lock_guard<std::mutex> _(ctx->lock);
                ctx->maybeLoadedImage = std::make_shared<ImageSRGBA>();
                // Assume it was already set and ignore it now.
                std::string filePath;
                reader.readImageBuffer(*ctx->maybeLoadedImage, filePath);
                zv_assert(ctx->clientSocket == _socket.get(), "Client socket changed!");
            }
            break;
        }

        default:
            break;
        }

        recvMessage ();
    }

private:
    ImageItemDataUniquePtr onLoadData (const ImageContextPtr& ctx)
    {
        auto dataPtr = std::make_unique<NetworkImageItemData>();
        dataPtr->cpuData = std::make_shared<ImageSRGBA>();

        // Did we get disconnected?
        if (!_socket)
        {
            dataPtr->status = ImageItemData::Status::FailedToLoad;
            return dataPtr;
        }
        
        dataPtr->impl->ctx = ctx;
        dataPtr->status = ImageItemData::Status::StillLoading;
        Message msg = requestImageBufferMessage (ctx->clientImageId);
        _senderQueue->enqueueMessage (std::move(msg));
        return dataPtr;
    }

    static Message requestImageBufferMessage(uint64_t imageIdInClient)
    {
        Message msg;
        msg.header.kind = MessageKind::RequestImageBuffer;
        msg.header.payloadSizeInBytes = sizeof(uint64_t);
        msg.payload.reserve(msg.header.payloadSizeInBytes);
        PayloadWriter w(msg.payload);
        w.appendUInt64(imageIdInClient);
        assert(msg.payload.size() == msg.header.payloadSizeInBytes);
        return msg;
    }    

private:
    zn::EventLoopPtr _eventLoop;
    zn::TcpSocketPtr _socket;

    zn::MessageReceiverPtr _receiver;
    zn::MessageSenderQueuePtr _senderQueue;

    // To share with the main thread.
    std::mutex _incomingImagesMutex;
    std::deque<IncomingImage> _incomingImages;
    
    // <ImageID in client, ImageItemData>
    std::unordered_map<uint64_t, ImageContextPtr> _availableImages;

    std::function<void(void)> _onDisconnectCb;
};
using ClientHandlerPtr = std::shared_ptr<ClientHandler>;

class ServerThread
{
public:    
    ~ServerThread()
    {
        stop ();
    }

    void updateMainThead (const Server::ImageReceivedCallback& imageReceivedCallback)
    {
        std::lock_guard<std::mutex> _(_clientHandlersMutex);
        for (auto& client : _clientHandlers)
            client.second->updateMainThead (imageReceivedCallback);
    }

    bool start (const std::string& hostname, int port)
    {
        _eventLoop = std::make_shared<zn::EventLoop>();
        bool ok = _eventLoop->initialize ();
        if (!ok)
            return false;

        _accept = std::make_shared<zn::TcpAccept>();
        ok = _accept->initialize (_eventLoop);
        if (!ok)
            return false;

        ok = _accept->openAccept (hostname, port);
        if (!ok)
        {
            std::clog << "Could not start listening on port " << port << std::endl;
            return false;
        }

        _serverThread = std::thread([this]() {
            runLoop ();
        });

        return true;
    }

    void stop ()
    {
        if (_serverThread.joinable())
        {
            {
                std::lock_guard<std::mutex> _ (_eventLoopMutex);
                if (_eventLoop)
                    _eventLoop->post([this]() { disconnect(); });
            }
            _serverThread.join();
        }
    }

private:
    void onClientDisconnected (const zn::TcpSocketPtr& socket)
    {
        std::lock_guard<std::mutex> _ (_clientHandlersMutex);
        _clientHandlers.erase (socket);
    }

    void acceptNext ()
    {
        zn::TcpSocketPtr nextSocket = std::make_shared<zn::TcpSocket>();
        _accept->doAccept (nextSocket, [this](zn::NetErrorCode err, zn::TcpSocketPtr socket) {
            if (err != zn::NEC_SUCCESS)
            {
                disconnect ();
                return;
            }

            ClientHandlerPtr client = std::make_shared<ClientHandler>();
            
            {
                std::lock_guard<std::mutex> _(_clientHandlersMutex);
                _clientHandlers[socket] = client;
            }

            client->start(_eventLoop, socket, [this, socket]()
                          { onClientDisconnected(socket); });

            acceptNext();
        });
    }

    void runLoop ()
    {
        acceptNext ();

        while (!_shouldStop)
        {
            // onMessage will be called once read.
            bool success = _eventLoop->runOnce ();
            if (!success)
                disconnect ();
        }

        std::lock_guard<std::mutex> _ (_eventLoopMutex);
        _eventLoop.reset ();
    }

    void disconnect ()
    {
        _shouldStop = true;

        std::lock_guard<std::mutex> _(_clientHandlersMutex);
        for (auto& client : _clientHandlers)
            client.second->stop ();
        
        _clientHandlers.clear ();
        _accept.reset ();
    }

private:
    std::thread _serverThread;
    bool _shouldStop = false;

    std::mutex _eventLoopMutex;
    zn::EventLoopPtr _eventLoop;
    zn::TcpAcceptPtr _accept;
    
    std::mutex _clientHandlersMutex;
    std::unordered_map<zn::TcpSocketPtr, ClientHandlerPtr> _clientHandlers;
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

bool Server::start (const std::string& hostname, int port)
{
    return impl->_serverThread.start (hostname, port);    
}

void Server::stop ()
{
    impl->_serverThread.stop ();
}

void Server::updateOnce (const ImageReceivedCallback &callback)
{
    impl->_serverThread.updateMainThead (callback);
}

} // zv
