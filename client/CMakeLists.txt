add_executable(client-test WIN32 main.cpp Client.h Client.cpp)
target_link_libraries(client-test pthread)
