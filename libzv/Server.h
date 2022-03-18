//
// Copyright (c) 2021, Nicolas Burrus
// This software may be modified and distributed under the terms
// of the BSD license.  See the LICENSE file for details.
//

#pragma once

#include <libzv/ImageList.h>

#include <memory>
#include <functional>

namespace zv
{

class Server
{
public:
    Server();
    ~Server();
    
public:
    void start (const std::string& hostname = "127.0.0.1", int port = 4207);

    void stop ();
    
    // Call the callbacks, etc. in the calling thread. This
    // avoids having to handle callbacks from any thread.
    void updateOnce ();

    using ImageReceivedCallback = std::function<void(ImageItemUniquePtr, int flags)>;
    void setImageReceivedCallback (const ImageReceivedCallback& callback);

private:
    struct Impl;
    friend struct Impl;
    std::unique_ptr<Impl> impl;
};

} // zv
