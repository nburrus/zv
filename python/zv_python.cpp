#include <libzv/Viewer.h>

#include <pybind11/pybind11.h>

using namespace zv;

#define STRINGIFY(x) #x
#define MACRO_STRINGIFY(x) STRINGIFY(x)

int add(int i, int j) {
    return i + j;
}

namespace py = pybind11;

PYBIND11_MODULE(_zv, m) {
    m.doc() = R"pbdoc(
        zv python module
        -----------------------
        .. currentmodule:: zv
        .. autosummary::
           :toctree: _generate
           add
    )pbdoc";

    py::class_<Viewer>(m, "Viewer")
        .def(py::init<>())
        .def("initialize", &Viewer::initialize)
        .def("exitRequested", &Viewer::exitRequested)
        .def("renderFrame", &Viewer::renderFrame)
        .def("addImageFromFile", &Viewer::addImageFromFile)
        .def("addImage", [](Viewer& viewer, const std::string& name, py::buffer buffer, int position, bool replace) {
            /* Request a buffer descriptor from Python */
            py::buffer_info info = buffer.request();

            /* Some sanity checks ... */
            if (info.format != py::format_descriptor<uint8_t>::format())
                throw std::runtime_error("Incompatible format: expected a uint8 array!");

            if (info.ndim != 3)
                throw std::runtime_error("Incompatible buffer dimension!");

            const int numRows = info.shape[0];
            const int numCols = info.shape[1];
            const int numChannels = info.shape[2];

            if (numChannels != 4)
                throw std::runtime_error("Channel size must be 4 for sRGBA");

            // uint8_t* otherData,
            // int otherWidth,
            // int otherHeight,
            // int otherBytesPerRow,
            ImageSRGBA image ((uint8_t*)info.ptr, numCols, numRows, info.strides[0], ImageSRGBA::noopReleaseFunc());
            viewer.addImageData (image, name, position, replace);
        }, py::arg("name"), py::arg("buffer"), py::arg("position") = -1, py::arg("replace") = false);

// PYTHON_VERSION_INFO comes from setup.py
#ifdef PYTHON_VERSION_INFO
    m.attr("__version__") = MACRO_STRINGIFY(PYTHON_VERSION_INFO);
#else
    m.attr("__version__") = "dev";
#endif
}
