#include "zv/Client.h"

#include "GeneratedConfig.h"

#include <argparse.hpp>

#include <thread>
#include <chrono>
#include <iostream>
#include <vector>

using namespace std::chrono_literals;

int main (int argc, char** argv)
{
    argparse::ArgumentParser argsParser("zv-client", PROJECT_VERSION);
    argsParser.add_argument("images")
        .help("Images to visualize")
        .remaining();

    argsParser.add_argument("-p", "--port")
        .help("Port number")
        .required()
        .scan<'i', int>()
        .default_value(4207);

    argsParser.add_argument("--host")
        .help("Server host or ip")
        .required()
        .default_value(std::string("127.0.0.1"));

    try
    {
        argsParser.parse_args(argc, argv);
    }
    catch (const std::runtime_error &err)
    {
        std::cerr << "Wrong usage" << std::endl;
        std::cerr << err.what() << std::endl;
        std::cerr << argsParser;
        return 1;
    }

    zv::Client client;
    if (!client.connect (argsParser.get<std::string>("--host"), argsParser.get<int>("--port")))
    {
        return 1;
    }

    try
    {
        auto images = argsParser.get<std::vector<std::string>>("images");
        fprintf(stderr, "%d images provided", (int)images.size());

        for (const auto &im : images)
            client.addImageFromFile(client.nextUniqueId(), im);
    }
    catch (const std::exception &err)
    {
        fprintf(stderr, "No images provided, the client has nothing to do.");
        return 1;
    }

    client.waitUntilDisconnected ();
    return 0;
}
