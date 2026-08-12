add_executable(client-test main.cpp zv/Client.h zv/Client.cpp zv/znet.cpp)
if (UNIX)
    target_link_libraries(client-test pthread)
endif()

if (WIN32)
    target_link_options(client-test PRIVATE "/ENTRY:mainCRTStartup")
endif()