add_executable(zv-client WIN32 main.cpp ../client/zv/Client.cpp ../client/zv/znet.cpp)
target_link_libraries(zv-client pthread)
target_include_directories(zv-client PRIVATE ../client)
