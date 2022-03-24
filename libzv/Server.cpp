//
// Copyright (c) 2017, Nicolas Burrus
// This software may be modified and distributed under the terms
// of the BSD license.  See the LICENSE file for details.
//

#include "Server.h"

#include <libzv/Utils.h>
#include <libzv/ImageList.h>

#include <client-kissnet/kissnet_zv.h>
#include <client-kissnet/Message.h>

#include <set>
#include <deque>
#include <thread>
#include <condition_variable>
#include <iostream>

#include <libzv/Platform.h>
#if PLATFORM_UNIX
# include <signal.h>
#endif

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

            zv_dbg ("[DEBUG][WRITER] Got event, checking if anything to send.");
            std::deque<Message> messagesToSend;
            messagesToSend.swap(_outputQueue);
            lk.unlock ();

            while (!messagesToSend.empty())
            {
                try
                {
                    Message msg = std::move(messagesToSend.front());
                    zv_dbg ("Sending message kind %d", (int)msg.header.kind);
                    sendMessage(*_socket, std::move(msg));
                    messagesToSend.pop_front();
                }
                catch (const std::exception& e)
                {
                    zv_dbg ("Got an exception, stopping the connection: %s", e.what());
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

struct ConnectionSubject
{
    virtual ~ConnectionSubject() {}
};

struct ConnectionObserver
{
    virtual void onConnectionEnded (ConnectionSubject*) = 0;
};

struct ServerConnectionThread : public ConnectionSubject
{
    ~ServerConnectionThread()
    {
        stop ();
    }

    struct IncomingImage
    {
        ImageItemUniquePtr item;
        uint32_t flags;
    };

    void start (ConnectionObserver* observer, std::unique_ptr<kn::tcp_socket> socket)
    {
        _observer = observer;
        _clientSocket = std::move(socket);
        _thread = std::thread([this]() {
            run ();
        });
    }

    void stop ()
    {
        _shouldDisconnect = true;
        _writerThread.enqueueMessage (closeMessage());
        _writerThread.stop ();

        // Kill the socket to break blocking recvs.
        _clientSocket->shutdown();
        _clientSocket->close();

        if (_thread.joinable())
            _thread.join();
    }

    void run ()
    {
        try
        {
            _writerThread.start (_clientSocket.get());

            while (!_shouldDisconnect)
            {
                Message msg = recvMessage ();
                zv_dbg ("Received message kind=%d", (int)msg.header.kind);
                switch (msg.header.kind)
                {
                case MessageKind::Close: {
                    if (!_shouldDisconnect)
                    {
                        _writerThread.enqueueMessage (closeMessage ());
                    }
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
                    
                    ImageSRGBA imageContent;
                    reader.readImageBuffer (imageContent);
                    if (imageContent.hasData())
                    {
                        imageItem->source = ImageItem::Source::Data;
                        imageItem->sourceData = std::make_shared<ImageSRGBA>(std::move(imageContent));
                    }
                    else
                    {
                        ImageContextPtr ctx = std::make_shared<ImageContext>();
                        ctx->clientImageId = clientImageId;
                        ctx->clientSocket = _clientSocket.get();
                        _availableImages[clientImageId] = ctx;
                        imageItem->source = ImageItem::Source::Callback;
                        imageItem->loadDataCallback = [this, ctx]()
                        {
                            return this->onLoadData(ctx);
                        };
                    }

                    {
                        std::lock_guard<std::mutex> lk (_incomingImagesMutex);
                        _incomingImages.push_back(IncomingImage {std::move(imageItem), flags});
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

            zv_dbg("Stopping a connection.");
            _writerThread.stop();

            if (_clientSocket)
            {
                _clientSocket->close();
                _clientSocket.reset();
            }
        }
        catch (std::exception &e)
        {
            std::cerr << "Server connection got exception: " << e.what() << std::endl;
        }

        if (_observer)
            _observer->onConnectionEnded (this);
    }
    
public:
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
    ImageItemDataUniquePtr onLoadData (const ImageContextPtr& ctx)
    {
        auto dataPtr = std::make_unique<NetworkImageItemData>();
        dataPtr->impl->ctx = ctx;
        dataPtr->status = ImageItemData::Status::StillLoading;
        dataPtr->cpuData = std::make_shared<ImageSRGBA>();
        Message msg = requestImageBufferMessage (ctx->clientImageId);
        _writerThread.enqueueMessage (std::move(msg));
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

    Message recvMessage ()
    {
        KnReader r (*_clientSocket);
        Message msg;
        msg.header.kind = (MessageKind)r.recvUInt32 ();
        msg.header.payloadSizeInBytes = r.recvUInt64 ();
        msg.payload.resize (msg.header.payloadSizeInBytes);
        if (msg.header.payloadSizeInBytes > 0)
            r.recvAllBytes (msg.payload.data(), msg.header.payloadSizeInBytes);
        return msg;
    }

private:
    ConnectionObserver* _observer = nullptr;
    std::thread _thread;
    bool _shouldDisconnect = false;
    std::unique_ptr<kn::tcp_socket> _clientSocket;

    ServerWriterThread _writerThread;

    std::mutex _incomingImagesMutex;
    std::deque<IncomingImage> _incomingImages;
    
    // <ImageID in client, ImageItemData>
    std::unordered_map<uint64_t, ImageContextPtr> _availableImages;
};

struct ServerThread : public ConnectionObserver
{
    ~ServerThread()
    {
        stop ();
    }

    void start (const std::string& hostname, int port)
    {
        // https://riptutorial.com/posix/example/17424/handle-sigpipe-generated-by-write---in-a-thread-safe-manner
        // https://stackoverflow.com/questions/23889062/c-how-to-handle-sigpipe-in-a-multithreaded-environment
#if PLATFORM_UNIX
        sigset_t sig_block, sig_restore, sig_pending;
        sigemptyset(&sig_block);
        sigaddset(&sig_block, SIGPIPE);
        if (pthread_sigmask(SIG_BLOCK, &sig_block, &sig_restore) != 0) 
        {
            zv_assert (false, "Could not block sigmask");
        }
#endif

        _serverSocket = kn::tcp_socket(kn::endpoint(hostname, port));
        _listenThread = std::thread([this, hostname, port]() {
            run ();
        });
    }

    void stop ()
    {
        _shouldStop = true;
        
        // Force pending accept calls to stop.
        // Calling shutdown first seems more robust.
        // https://stackoverflow.com/questions/4160347/close-vs-shutdown-socket
        _serverSocket.shutdown ();
        _serverSocket.close ();

        if (_listenThread.joinable())
            _listenThread.join();
    }

    void startClientThread (std::unique_ptr<kn::tcp_socket> s)
    {
        auto* clientThread = new ServerConnectionThread();
        std::lock_guard<std::mutex> lk (_clientThreadsMutex);
        _clientThreads.insert (clientThread);
        clientThread->start (this, std::move(s));
    }

    virtual void onConnectionEnded (ConnectionSubject* connSubject) override    
    {
        zv_dbg ("Got notified that a client disconnected");

        ServerConnectionThread* conn = dynamic_cast<ServerConnectionThread*>(connSubject);
        zv_assert(conn, "Invalid connection !");

        {
            std::lock_guard<std::mutex> lk (_clientThreadsMutex);
            _deadThreads.push_back(conn);
        }
    }

    void run ()
    {
        try
        {
            _serverSocket.set_reuseaddr (true);
            _serverSocket.bind();
            _serverSocket.listen();

            while (!_shouldStop)
            {
                startClientThread (std::make_unique<kn::tcp_socket>(_serverSocket.accept()));

                // Periodic cleanup.
                cleanDeadThreads ();
            }
        }
        catch (std::exception &e)
        {
            zv_dbg ("Server got exception: %s", e.what());
        }

        for (auto& client : _clientThreads)
        {
            client->stop ();
        }
        cleanDeadThreads ();

        for (auto* client : _clientThreads)
            delete client;
        _clientThreads.clear ();

        _serverSocket.close ();
    }
    
public:
    
    void updateMainThead (const Server::ImageReceivedCallback& imageReceivedCallback)
    {
        std::lock_guard<std::mutex> lk (_clientThreadsMutex);
        for (auto& client : _clientThreads)
            client->updateMainThead (imageReceivedCallback);
    }

private:
    void cleanDeadThreads()
    {
        // Periodic removal of dead threads.
        {
            std::lock_guard<std::mutex> lk (_clientThreadsMutex);
            for (const auto& conn : _deadThreads)
            {
                _clientThreads.erase(conn);
                delete conn;
            }
            _deadThreads.clear();
        }
    }

private:
    std::thread _listenThread;
    bool _shouldStop = false;

    kn::tcp_socket _serverSocket;
    
    std::mutex _clientThreadsMutex;
    std::set<ServerConnectionThread*> _clientThreads;
    std::vector<ServerConnectionThread*> _deadThreads;
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
    impl->_serverThread.stop ();
}

void Server::updateOnce (const ImageReceivedCallback &callback)
{
    impl->_serverThread.updateMainThead (callback);
}

} // zv
