add_executable(zv-client main.cpp ../client/zv/Client.cpp ../client/zv/znet.cpp)
target_include_directories(zv-client PRIVATE ../client)

if (UNIX)
    target_link_libraries(zv-client pthread)
endif()

if (WIN32)
    target_link_options(zv-client PRIVATE "/ENTRY:mainCRTStartup")
endif()
