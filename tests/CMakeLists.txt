enable_testing()

# Add doctest include directory
include_directories(${CMAKE_SOURCE_DIR}/deps/doctest)

# Create test executable
add_executable(test_utils test_utils.cpp)

# Link against libzv
target_link_libraries(test_utils zv)

# Add test to CTest
add_test(NAME test_utils COMMAND test_utils)
