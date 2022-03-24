add_executable(zv-client-kn WIN32 main.cpp Client.h Client.cpp)
target_link_libraries(zv-client-kn pthread)