enable_testing()

# Add doctest include directory
include_directories(${CMAKE_SOURCE_DIR}/deps/doctest)

# Create test executables
add_executable(test_utils test_utils.cpp)
add_executable(test_color_editor test_color_editor.cpp)
add_executable(test_annotations test_annotations.cpp)

# Link against libzv
target_link_libraries(test_utils zv)
target_link_libraries(test_color_editor zv)
target_link_libraries(test_annotations zv)

# Add tests to CTest
add_test(NAME test_utils COMMAND test_utils)
add_test(NAME test_color_editor COMMAND test_color_editor)
add_test(NAME test_annotations COMMAND test_annotations)
