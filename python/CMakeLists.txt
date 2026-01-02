# Create an extension module using pybind11 helper
# This handles platform-specific extension suffix (.cpython-310-darwin.so, etc.)
pybind11_add_module(_zv MODULE zv_python.cpp ../client/zv/Client.cpp)
target_link_libraries(_zv PRIVATE zv)

# Set output directory if specified (used by setup.py for pip install)
if(CMAKE_LIBRARY_OUTPUT_DIRECTORY)
    set_target_properties(_zv PROPERTIES
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_LIBRARY_OUTPUT_DIRECTORY}"
        LIBRARY_OUTPUT_DIRECTORY_DEBUG "${CMAKE_LIBRARY_OUTPUT_DIRECTORY}"
        LIBRARY_OUTPUT_DIRECTORY_RELEASE "${CMAKE_LIBRARY_OUTPUT_DIRECTORY}"
    )
endif()

target_compile_definitions(_zv PRIVATE PYTHON_VERSION_INFO=${PYTHON_VERSION_INFO})
