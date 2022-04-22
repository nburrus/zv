add_executable(client-test WIN32 main.cpp zv/Client.h zv/Client.cpp zv/znet.cpp)
target_link_libraries(client-test pthread)
