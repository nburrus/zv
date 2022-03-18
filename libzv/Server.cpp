//
// Copyright (c) 2017, Nicolas Burrus
// This software may be modified and distributed under the terms
// of the BSD license.  See the LICENSE file for details.
//

#include "Server.h"

#include <libzv/Utils.h>

#include <kissnet.hpp>

#include <thread>
#include <iostream>

namespace zv
{

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
            
        }
        catch (std::exception &e)
        {
            std::cerr << "Server got exception: " << e.what() << std::endl;
        }
    }

    std::thread _thread;
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
