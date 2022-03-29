add_executable(zv-client WIN32 main.cpp ../client/Client.h ../client/Client.cpp)
target_link_libraries(zv-client pthread)
target_include_directories(zv-client PRIVATE ../client)
