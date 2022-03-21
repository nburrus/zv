//
// Copyright (c) 2017, Nicolas Burrus
// This software may be modified and distributed under the terms
// of the BSD license.  See the LICENSE file for details.
//

#include "Client.h"

#include "kissnet_zv.h"
#include "Message.h"

#include <thread>
#include <mutex>
#include <condition_variable>
#include <chrono>
#include <deque>
#include <cassert>
#include <iostream>

using namespace std::chrono_literals;
namespace kn = kissnet;

namespace zv
{

class ClientWriteThread
{
public:
    ClientWriteThread ()
    {
        
    }

    ~ClientWriteThread ()
    {
        stop ();
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
    std::mutex _outputQueueMutex;
    std::deque<Message> _outputQueue;
    std::condition_variable _messageAdded;
};

class ClientThread
{
public:
    ~ClientThread()
    {
        stop ();
    }

    void start(const std::string &hostname, int port)
    {
        _socket = kn::tcp_socket(kn::endpoint(hostname, port));
        _thread = std::thread([this]() { runMainLoop(); });
    }

    void stop ()
    {
        _shouldDisconnect = true;
        enqueueMessage (closeMessage());
        if (_thread.joinable())
            _thread.join();
    }

    void enqueueMessage (Message&& msg)
    {
        return _writeThread.enqueueMessage (std::move(msg));
    }

private: 
    // The main loop will keep reading.
    // The write loop will keep writing.
    void runMainLoop ()
    {
        _socket.connect ();

        _writeThread.start (&_socket);
        _writeThread.enqueueMessage (versionMessage(1));

        while (!_shouldDisconnect)
        {       
            try 
            {
                Message msg = recvMessage();
                switch (msg.kind)
                {
                case MessageKind::Invalid:
                {
                    std::clog << "[DEBUG][READER] Invalid message" << std::endl;
                    _shouldDisconnect = true;
                    break;
                }

                case MessageKind::Close:
                {
                    std::clog << "[DEBUG][READER] got close message" << std::endl;
                    if (!_shouldDisconnect)
                    {
                        _writeThread.enqueueMessage (closeMessage ());
                    }
                    _shouldDisconnect = true;
                    break;
                }

                case MessageKind::Version:
                {
                    PayloadReader r (msg.payload);
                    int32_t serverVersion = r.readInt32();
                    std::clog << "[DEBUG][READER] Server version = " << serverVersion << std::endl;
                    assert(serverVersion == 1);
                    break;
                }
                }
            }
            catch (const std::exception& e)
            {
                std::clog << "Got an exception, stopping the connection: " << e.what() << std::endl;
                break;
            }
        }

        _writeThread.stop ();
        _socket.close ();
    }

private:
    Message recvMessage ()
    {
        KnReader r (_socket);
        Message msg;
        msg.kind = (MessageKind)r.recvUInt32 ();
        msg.payloadSizeInBytes = r.recvUInt64 ();
        msg.payload.resize (msg.payloadSizeInBytes);
        if (msg.payloadSizeInBytes > 0)
            r.recvAllBytes (msg.payload.data(), msg.payloadSizeInBytes);
        return msg;
    }

private: 
    std::thread _thread; 
    ClientWriteThread _writeThread;

    kn::tcp_socket _socket;
    bool _shouldDisconnect = false;
};

struct ClientPayloadWriter : PayloadWriter
{
    ClientPayloadWriter(std::vector<uint8_t>& payload) : PayloadWriter (payload) {}

    void appendImageBuffer (const Client::ImageView& imageBuffer)
    {
        appendUInt32 (imageBuffer.width);
        appendUInt32 (imageBuffer.height);
        appendUInt32 (imageBuffer.bytesPerRow);
        appendBytes (imageBuffer.pixels_RGBA32, imageBuffer.numBytes());
    }
};

struct Client::Impl
{
    ClientThread _clientThread;
};

Client::Client(const std::string& hostname, int port) : impl (new Impl())
{
    impl->_clientThread.start (hostname, port);
}

Client::~Client() = default;

void Client::addImage (uint64_t imageId, const std::string& imageName, const ImageView& imageBuffer, bool replaceExisting)
{
    // uniqueId:uint64_t name:StringUTF8 flags:uint32_t imageBuffer:ImageBuffer
    // ImageBuffer
    //      format: uint32_t
    //      width:uint32_t
    //      height:uint32_t
    //      bytesPerRow:uint32_t
    //      buffer:Blob
    Message msg;
    msg.kind = MessageKind::Image;
    msg.payloadSizeInBytes = (
        sizeof(uint64_t) // imageId
        + imageName.size() + sizeof(uint64_t) // name
        + sizeof(uint32_t) // flags
        + sizeof(uint32_t)*3 + imageBuffer.numBytes() // image buffer
    );
    msg.payload.reserve (msg.payloadSizeInBytes);

    uint32_t flags = replaceExisting;
    ClientPayloadWriter w (msg.payload);
    w.appendUInt64 (imageId);
    w.appendStringUTF8 (imageName);    
    w.appendUInt32 (flags);
    w.appendImageBuffer (imageBuffer);
    assert (msg.payload.size() == msg.payloadSizeInBytes);

    impl->_clientThread.enqueueMessage (std::move(msg));
}

} // zv
