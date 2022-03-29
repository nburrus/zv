#include "Client.h"

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

    try
    {
        argsParser.parse_args(argc, argv);
    }
    catch (const std::runtime_error &err)
    {
        std::cerr << "Wrong usage" << std::endl;
        std::cerr << err.what() << std::endl;
        std::cerr << argsParser;
        return false;
    }

    zv::Client client;
    if (!client.connect ("127.0.0.1", 4207))
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
        return false;
    }

    client.waitUntilDisconnected ();
    return 0;
}
