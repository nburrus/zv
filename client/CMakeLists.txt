add_executable(client-test WIN32 main.cpp Client.h Client.cpp znet.cpp)
target_link_libraries(client-test pthread)
