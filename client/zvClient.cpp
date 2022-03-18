#include "zvClient.h"

#include "kissnet.hpp"

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

struct connection_closed_exception : public std::exception
{
	const char * what () const throw ()
    {
    	return "Connexion was closed.";
    }
};

// Make sure that all the bytes got sent.
void sendAllBytes (kn::tcp_socket& s, const uint8_t* input_bytes, size_t numBytes)
{
    const std::byte* bytes = reinterpret_cast<const std::byte*>(input_bytes);
    size_t bytesSent = 0;
    while (bytesSent < numBytes)
    {
        auto [sent_bytes, status] = s.send (bytes + bytesSent, numBytes - bytesSent);
        if (!status)
            throw connection_closed_exception();
        bytesSent += sent_bytes;
    }
}

template <class T>
void sendValue (kn::tcp_socket& s, const T& value) { sendAllBytes (s, reinterpret_cast<const uint8_t*>(&value), sizeof(T)); }

inline void sendInt32 (kn::tcp_socket& s, int32_t value) { sendValue(s, value); }
inline void sendUInt32 (kn::tcp_socket& s, uint32_t value) { sendValue(s, value); }
inline void sendUInt64 (kn::tcp_socket& s, uint64_t value) { sendValue(s, value); }

void recvAllBytes (kn::tcp_socket& s, uint8_t* output_bytes, size_t numBytes)
{
    std::byte* bytes = reinterpret_cast<std::byte*>(output_bytes);
    size_t bytesRead = 0;
    while (bytesRead < numBytes)
    {
        auto [received_bytes, status] = s.recv(bytes + bytesRead, numBytes - bytesRead);
        if (!status)
            throw connection_closed_exception();
        bytesRead += received_bytes;
    }
}

template <class T>
T recvValue (kn::tcp_socket& s)
{
    T value;
    uint8_t* bytes = reinterpret_cast<uint8_t*>(&value);
    recvAllBytes (s, bytes, sizeof(T));
    return value;
}

uint32_t recvInt32 (kn::tcp_socket& s) { return recvValue<int32_t>(s); }
uint32_t recvUInt32 (kn::tcp_socket& s) { return recvValue<uint32_t>(s); }
uint64_t recvUInt64 (kn::tcp_socket& s) { return recvValue<uint64_t>(s); }

template <class T>
void appendValueToPayload (std::vector<uint8_t>& payload, const T& value)
{
    const uint8_t* valuePtr = reinterpret_cast<const uint8_t*>(&value);
    std::copy (valuePtr, valuePtr + sizeof(value), std::back_inserter(payload));
}

void appendInt32 (std::vector<uint8_t>& payload, int32_t value) { appendValueToPayload(payload, value); }
void appendUInt32 (std::vector<uint8_t>& payload, uint32_t value) { appendValueToPayload(payload, value); }
void appendUInt64 (std::vector<uint8_t>& payload, uint64_t value) { appendValueToPayload(payload, value); }

void appendBytes (std::vector<uint8_t>& payload, const uint8_t* bytes, size_t numBytes)
{
    std::copy (bytes, bytes + numBytes, std::back_inserter(payload));
}

// Bytes + a size.
void appendBlob (std::vector<uint8_t>& payload, const uint8_t* bytes, size_t numBytes)
{
    appendUInt64(payload, numBytes);
    appendBytes (payload, bytes, numBytes);
}

void appendStringUTF8 (std::vector<uint8_t>& payload, const std::string& s)
{ 
    appendBlob (payload, reinterpret_cast<const uint8_t*>(s.c_str()), s.size());
}

void appendImageBuffer (std::vector<uint8_t>& payload, const Client::ImageView& imageBuffer)
{
    appendUInt32 (payload, imageBuffer.width);
    appendUInt32 (payload, imageBuffer.height);
    appendUInt32 (payload, imageBuffer.bytesPerRow);
    appendBytes (payload, imageBuffer.pixels_RGBA32, imageBuffer.numBytes());
}

template <class T>
T readValue (std::vector<uint8_t>& payload, size_t& offset)
{
    T v;
    uint8_t* outputPtr = reinterpret_cast<uint8_t*>(&v);
    if (payload.size() < offset + sizeof(T))
    {
        throw std::overflow_error("Could not read an expected value.");
    }
    std::copy (payload.begin() + offset, payload.begin() + offset + sizeof(T), outputPtr);
    offset += sizeof(T);
    return v;
}

int32_t readInt32 (std::vector<uint8_t>& payload, size_t& offset) { return readValue<int32_t> (payload, offset); }
uint32_t readUInt32 (std::vector<uint8_t>& payload, size_t& offset) { return readValue<uint32_t> (payload, offset); }
uint64_t readUInt64 (std::vector<uint8_t>& payload, size_t& offset) { return readValue<uint64_t> (payload, offset); }

void readBlob (std::vector<uint8_t>& payload, size_t& offset, std::vector<uint8_t>& outputData)
{
    size_t numBytes = readUInt64 (payload, offset);

    if (payload.size() < offset + numBytes)
    {
        throw std::overflow_error("Could not read an expected blob.");
    }

    outputData.resize (numBytes);
    std::copy (payload.begin() + offset, payload.begin() + offset + numBytes, outputData.begin());
    offset += numBytes;
}

void readStringUTF8 (std::vector<uint8_t>& payload, size_t& offset, std::string& s)
{
    size_t numBytes = readUInt64 (payload, offset);

    if (payload.size() < offset + numBytes)
    {
        throw std::overflow_error("Could not read an expected string.");
    }

    s.assign((const char*)payload.data() + offset, numBytes);
    offset += numBytes;
}

/**
    PROTOCOL

    All encodings are expected to be transmitted as little endian.
    Big-endian platforms will need adjustments.

    = Overview =

    Communications are asynchronous and message-based.
    Both the client and server can send and receive messages
    simultaneously.

    = Message Structure =

    Message:
        kind: uint32_t
        contentSizeInBytes: uint64_t
        content: uint8_t[]

    The message content itself will be encoded depending on
    the message kind. See the enum comments in MessageKind.

    = Basic Types =

    StringUTF8: 
        sizeInBytes:uint32_t
        chars:uint8_t[]

    Blob: 
        sizeInBytes:uint64_t
        data:uint8_t[]

    ImageBuffer:
        format: uint32_t
        width:uint32_t
        height:uint32_t
        bytesPerRow:uint32_t
        buffer:Blob
*/

enum class MessageKind : int32_t
{
    Invalid = -1,

    // No payload. Just request a close message.
    Close = 0,

    // version:uint32_t
    Version = 1,

    // Sending a new image. If the ImageContent has a zero width and height then
    // the server knows it'll need to request the data with GetImageData when
    // it'll need it. This is useful when telling the server about many
    // available images (e.g listing a folder). 
    //
    // uniqueId:uint64_t name:StringUTF8 flags:uint32_t imageBuffer:ImageBuffer
    Image = 2,

    // Request image data.
    // uniqueId:uint64_t
    RequestImageBuffer = 3, // the server needs the data for the given image name.

    // Output image data.
    // uniqueId:uint64_t imageBuffer:ImageBuffer
    ImageBuffer = 4,
};

struct Message
{
    Message () {}

    // Make sure we don't accidentally make copies by leaving
    // only the move operators.
    Message (Message&& msg) = default;
    Message& operator= (Message&& msg) = default;

    MessageKind kind = MessageKind::Invalid;
    uint64_t payloadSizeInBytes = 0;
    std::vector<uint8_t> payload;
};

Message closeMessage()
{
    Message msg;
    msg.kind = MessageKind::Close;
    msg.payloadSizeInBytes = 0;
    return msg;
}

Message versionMessage(int32_t version)
{
    Message msg;
    msg.kind = MessageKind::Version;
    msg.payloadSizeInBytes = sizeof(uint32_t);
    msg.payload.reserve(msg.payloadSizeInBytes);
    appendInt32(msg.payload, version);
    assert (msg.payload.size() == msg.payloadSizeInBytes);
    return msg;
}

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
                    sendMessage(std::move(messagesToSend.front()));
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

    void sendMessage (const Message& msg)
    {
        sendInt32 (*_socket, (int32_t)msg.kind);
        sendUInt64 (*_socket, msg.payloadSizeInBytes);
        std::clog << "[DEBUG][WRITER] Payload sent: " << msg.payloadSizeInBytes << std::endl;
        if (msg.payloadSizeInBytes > 0)
            _socket->send ((std::byte*)msg.payload.data(), msg.payloadSizeInBytes);
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
                    size_t offset = 0;
                    int32_t serverVersion = readInt32(msg.payload, offset);
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
        // const auto [data_size, status_code] = _socket.recv(static_buffer);
        Message msg;
        msg.kind = (MessageKind)recvUInt32 (_socket);
        msg.payloadSizeInBytes = recvUInt64 (_socket);
        msg.payload.resize (msg.payloadSizeInBytes);
        if (msg.payloadSizeInBytes > 0)
            recvAllBytes (_socket, msg.payload.data(), msg.payloadSizeInBytes);
        return msg;
    }

private: 
    std::thread _thread; 
    ClientWriteThread _writeThread;

    kn::tcp_socket _socket;
    bool _shouldDisconnect = false;
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
    appendUInt64 (msg.payload, imageId);
    appendStringUTF8 (msg.payload, imageName);    
    appendUInt32 (msg.payload, flags);
    appendImageBuffer (msg.payload, imageBuffer);
    assert (msg.payload.size() == msg.payloadSizeInBytes);

    impl->_clientThread.enqueueMessage (std::move(msg));
}

} // zv
