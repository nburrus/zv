//
// Copyright (c) 2017, Nicolas Burrus
// This software may be modified and distributed under the terms
// of the BSD license.  See the LICENSE file for details.
//

#include "AppClient.h"

#include <libzv/Utils.h>

#include <client/znet_zv.h>
#include <client/Message.h>

#include <thread>
#include <mutex>
#include <condition_variable>
#include <chrono>
#include <deque>
#include <cassert>
#include <iostream>
#include <atomic>

namespace zn = zsummer::network;

namespace zv
{

struct AppClientPayloadWriter : PayloadWriter
{
    AppClientPayloadWriter(std::vector<uint8_t>& payload) : PayloadWriter (payload) {}

    void appendImageBuffer (const ImageSRGBA& image)
    {
        appendUInt32 (image.width());
        appendUInt32 (image.height());
        appendUInt32 (image.bytesPerRow());
        if (image.bytesPerRow() > 0)
            appendBytes (image.rawBytes(), image.sizeInBytes());
    }
};

class MessageImageWriter : public ImageWriter
{
public:
    MessageImageWriter (Message& msg, uint64_t imageId) : msg (msg), writer (msg.payload)
    {
        msg.header.kind = MessageKind::ImageBuffer;
        writer.appendUInt64 (imageId);
    }

    ~MessageImageWriter ()
    {
        msg.header.payloadSizeInBytes = msg.payload.size();
    }

    virtual void write (const ImageSRGBA& image) override
    {
        writer.appendImageBuffer (image);
    }

private:
    Message& msg;
    AppClientPayloadWriter writer;
};

class AppClientThread
{
public:
    enum class Status
    {
        Init,
        Connecting,
        Connected,
        FailedToConnect,
        Disconnected,
    };

public:
    ~AppClientThread()
    {
        stop ();
    }

    bool isConnected () const
    {
        std::lock_guard<std::mutex> _ (_statusMutex);
        return _status == Status::Connected;
    }

    void waitUntilDisconnected ()
    {
        if (_thread.joinable())
            _thread.join ();
    }

    bool start(const std::string& hostname, int port)
    {
        _status = Status::Connecting;
        _thread = std::thread([this, hostname, port]() { runMainLoop(hostname, port); });
        
        std::unique_lock<std::mutex> lk (_statusMutex);
        _statusChanged.wait (lk, [this]() {
            return _status != Status::Connecting;
        });
        
        return _status == Status::Connected;
    }

    void stop ()
    {
        if (_thread.joinable())
        {
            {
                std::lock_guard<std::mutex> lk(_eventLoopMutex);
                if (_eventLoop)
                    _eventLoop->post([this]() { disconnect(); });
            }
            _thread.join();
        }
    }

    void addImage (uint64_t imageId, const std::string& imageName, const AppClient::GetDataCallback& getDataCallback, bool replaceExisting)
    {
        if (!isConnected())
            return;

        {
            std::lock_guard<std::mutex> lk(_getDataCallbacksMutex);
            assert(_getDataCallbacks.find(imageId) == _getDataCallbacks.end());
            _getDataCallbacks[imageId] = getDataCallback;
        }
        addImage(imageId, imageName, ImageSRGBA(), replaceExisting);
    }

    void addImage (uint64_t imageId, const std::string& imageName, const ImageSRGBA& imageBuffer, bool replaceExisting)
    {
        if (!isConnected())
            return;

        // uniqueId:uint64_t name:StringUTF8 flags:uint32_t imageBuffer:ImageBuffer
        // ImageBuffer
        //      format: uint32_t
        //      width:uint32_t
        //      height:uint32_t
        //      bytesPerRow:uint32_t
        //      buffer:Blob
        Message msg;
        msg.header.kind = MessageKind::Image;
        msg.header.payloadSizeInBytes = (
            sizeof(uint64_t) // imageId
            + imageName.size() + sizeof(uint64_t) // name
            + sizeof(uint32_t) // flags
            + sizeof(uint32_t)*3 + imageBuffer.sizeInBytes() // image buffer
        );
        msg.payload.reserve (msg.header.payloadSizeInBytes);

        uint32_t flags = replaceExisting;
        AppClientPayloadWriter w (msg.payload);
        w.appendUInt64 (imageId);
        w.appendStringUTF8 (imageName);    
        w.appendUInt32 (flags);
        w.appendImageBuffer (imageBuffer);
        assert (msg.payload.size() == msg.header.payloadSizeInBytes);

        _senderQueue->enqueueMessage (std::move(msg));
    }

private: 
    void disconnect ()
    {
        // Already disconnected.
        if (!_socket)
            return;

        _shouldDisconnect = true;
        _receiver.reset ();
        _senderQueue.reset ();
        _socket->doClose ();
        _socket.reset ();
        setStatus (Status::Disconnected);
    }

    void setStatus (Status status)
    {
        std::lock_guard<std::mutex> _ (_statusMutex);
        _status = status;
        _statusChanged.notify_all ();
    }

    void onMessage(zn::NetErrorCode error, const Message& msg)
    {        
        if (error != zn::NEC_SUCCESS)
            return disconnect();

        switch (msg.header.kind)
        {
        case MessageKind::Invalid:
        {
            std::clog << "[DEBUG][READER] Invalid message" << std::endl;
            disconnect ();
            break;
        }

        case MessageKind::Close:
        {
            std::clog << "[DEBUG][READER] got close message" << std::endl;
            disconnect ();
            break;
        }

        case MessageKind::Version:
        {
            PayloadReader r(msg.payload);
            int32_t serverVersion = r.readInt32();
            std::clog << "[DEBUG][READER] Server version = " << serverVersion << std::endl;
            assert(serverVersion == 1);
            break;
        }

        case MessageKind::RequestImageBuffer:
        {
            // uniqueId:uint64_t
            PayloadReader r(msg.payload);
            uint64_t imageId = r.readUInt64();

            Message outputMessage;
            {
                MessageImageWriter msgWriter(outputMessage, imageId);
                AppClient::GetDataCallback callback;
                {
                    std::lock_guard<std::mutex> lk(_getDataCallbacksMutex);
                    auto callbackIt = _getDataCallbacks.find(imageId);
                    assert(callbackIt != _getDataCallbacks.end());
                    callback = callbackIt->second;
                }

                if (callback)
                {
                    callback(msgWriter);
                }
            }
            _senderQueue->enqueueMessage(std::move(outputMessage));
            break;
        }

        default:
            break;
        }

        // Keep reading.
        recvMessage();
    }    

    // The main loop will keep reading.
    // The write loop will keep writing.
    void runMainLoop (const std::string &hostname, int port)
    {        
        _eventLoop = std::make_shared<zn::EventLoop>();
        _eventLoop->initialize ();
        _socket = std::make_shared<zn::TcpSocket>();
        bool ok = _socket->initialize (_eventLoop);
        if (!ok)
        {
            setStatus (Status::FailedToConnect);
            std::clog << "Could not initialize a socket." << std::endl;
            return;
        }

        ok = _socket->doConnect (hostname, port, [this](zn::NetErrorCode error) {
            if (error != zn::NEC_SUCCESS)
            {
                std::clog << "Could not connect to the ZV client." << std::endl;
                setStatus (Status::FailedToConnect);
                return disconnect ();
            }

            // Start the receive message loop.
            _receiver = std::make_shared<zn::MessageReceiver>(_socket);
            recvMessage ();

            _senderQueue = std::make_shared<zn::MessageSenderQueue>(_eventLoop, _socket, [this](zn::NetErrorCode err) {
                if (err != zn::NEC_SUCCESS)
                    disconnect ();
            });

            _senderQueue->enqueueMessage (versionMessage(1));

            setStatus (Status::Connected);
        });
        if (!ok)
        {
            std::clog << "Could not connect to the ZV client." << std::endl;
            return;
        }

        while (!_shouldDisconnect)
        {
            // onMessage will be called once read.
            bool success = _eventLoop->runOnce ();
            if (!success)
                disconnect ();
        }
        
        disconnect ();
        
        {
            std::lock_guard<std::mutex> lk(_eventLoopMutex);
            _eventLoop.reset();
        }
    }

    void recvMessage ()
    {
        if (!_receiver)
            return;
        _receiver->recvMessage([this](zn::NetErrorCode err, const Message &msg) { onMessage(err, msg); });
    }

private: 
    std::thread _thread; 
    
    std::mutex _eventLoopMutex;
    zn::EventLoopPtr _eventLoop;

    zn::TcpSocketPtr _socket;
    zn::MessageReceiverPtr _receiver;
    zn::MessageSenderQueuePtr _senderQueue;

    bool _shouldDisconnect = false;
    Status _status = Status::Init;
    std::condition_variable _statusChanged;
    mutable std::mutex _statusMutex;

    std::mutex _outputQueueMutex;
    std::deque<Message> _outputQueue;

    std::mutex _getDataCallbacksMutex;
    std::unordered_map<uint64_t, AppClient::GetDataCallback> _getDataCallbacks;
};

} // zv

namespace zv
{

struct AppClient::Impl
{
    AppClientThread _clientThread;
    uint64_t nextId = 0;
};

AppClient::AppClient() : impl (new Impl())
{    
}

AppClient::~AppClient() = default;

bool AppClient::connect (const std::string& hostname, int port)
{
    return impl->_clientThread.start (hostname, port); 
}

bool AppClient::isConnected () const
{
    return impl->_clientThread.isConnected ();
}

void AppClient::waitUntilDisconnected ()
{
    impl->_clientThread.waitUntilDisconnected ();
}

void AppClient::addImage (uint64_t imageId, const std::string& imageName, const ImageSRGBA& imageBuffer, bool replaceExisting)
{
    impl->_clientThread.addImage (imageId, imageName, imageBuffer, replaceExisting);
}

void AppClient::addImage (uint64_t imageId, const std::string& imageName, GetDataCallback&& getDataCallback, bool replaceExisting)
{
    impl->_clientThread.addImage (imageId, imageName, getDataCallback, replaceExisting);
}

void AppClient::addImageFromFile (const std::string& imPath)
{
    uint64_t imageId = impl->nextId++;
    auto cb = [imPath](ImageWriter& writer) {
        zv_dbg ("%s requested", imPath.c_str());
        ImageSRGBA image;
        bool ok = readPngImage (imPath, image);
        if (!ok)
            return false;
        zv_dbg ("Writing the image.");
        writer.write (image);
        return true;
    };
    addImage (imageId, imPath, std::move(cb), true);
}

} // zv