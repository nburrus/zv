add_executable(zv-client WIN32 main.cpp zvClient.h zvClient.cpp)
target_link_libraries(zv-client pthread)