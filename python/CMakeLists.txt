# Create an extension module
add_library(zvpy MODULE zv_python.cpp ../client/zv/Client.cpp)
set_target_properties(zvpy PROPERTIES OUTPUT_NAME _zv)
set_target_properties(zvpy PROPERTIES PREFIX "")
if (WIN32)
    set_target_properties(zvpy PROPERTIES SUFFIX ".pyd")
endif()
target_link_libraries(zvpy PUBLIC pybind11::module zv)

add_definitions(-DPYTHON_VERSION_INFO=${PYTHON_VERSION_INFO})
