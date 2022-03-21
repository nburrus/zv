#include "Client.h"

#include <thread>
#include <chrono>

using namespace std::chrono_literals;

int main ()
{
    zv::Client client;

    int w = 128;
    int h = 64;
    std::vector<uint8_t> imData (128*64*4);
    zv::Client::ImageView view (imData.data(), 128, 64);

    for (int i = 0; i < 5; ++i)
    {
        std::fill (imData.begin(), imData.end(), i*16);
        std::string name = "TestImage-" + std::to_string(i);
        client.addImage (i, name, view);
        std::this_thread::sleep_for(100ms);
    }

    std::this_thread::sleep_for(1000ms);

    return 0;
}