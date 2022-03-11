#include <libzv/App.h>
#include <libzv/Viewer.h>
#include <libzv/ColorConversion.h>

#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

using namespace zv;

#define STRINGIFY(x) #x
#define MACRO_STRINGIFY(x) STRINGIFY(x)

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

    py::class_<App>(m, "App")
        .def(py::init<>())

        .def("initialize", [](App& app, const std::vector<std::string>& argv) {
            return app.initialize (argv);
        }, py::arg("argv") = std::vector<std::string>({"zv"}))

        .def("numViewers", &App::numViewers)

        .def("getViewer", [](App& app, const std::string& name) {
            return app.getViewer (name);
        }, py::arg("name") = "default")

        .def("createViewer", &App::createViewer)

        .def("updateOnce", &App::updateOnce);

    py::class_<Viewer>(m, "Viewer")
        .def("addImageFromFile", &Viewer::addImageFromFile)

        .def("addImage", [](Viewer& viewer, const std::string& name, py::buffer buffer, int position, bool replace) {
            /* Request a buffer descriptor from Python */
            py::buffer_info info = buffer.request();

            ImageSRGBA image;

            if (info.ndim != 2 && info.ndim != 3)
                throw std::runtime_error("Image dimension must be 2 (grayscale) or 3 (color)");

            // (H,W,1) is the same as (H,W), treat it as grayscale.
            int actual_dims = info.ndim;
            if (info.ndim == 3 && info.shape[2] == 1)
                actual_dims = 2;

            const int numRows = info.shape[0];
            const int numCols = info.shape[1];

            switch (actual_dims)
            {
                case 2: 
                {
                    if (info.format == py::format_descriptor<uint8_t>::format())
                    {
                        image = srgbaFromGray ((uint8_t*)info.ptr, numCols, numRows, info.strides[0]);
                    }
                    else if (info.format == py::format_descriptor<float>::format())
                    {
                        image = srgbaFromFloatGray ((uint8_t*)info.ptr, numCols, numRows, info.strides[0]);
                    }
                    else
                    {
                        throw std::runtime_error("Grayscale images must have np.uint8 or np.float32 dtype.");
                    }
                    break;
                }

                case 3: {
                    const int numChannels = info.shape[2];
                    if (numChannels != 3 && numChannels != 4)
                        throw std::runtime_error("Channel size must be 3 (RGB) or 4 (RGBA)");

                    switch (numChannels)
                    {
                        case 3: {
                            if (info.format == py::format_descriptor<uint8_t>::format())
                            {
                                image = srgbaFromSrgb ((uint8_t*)info.ptr, numCols, numRows, info.strides[0]);
                            }
                            else if (info.format == py::format_descriptor<float>::format())
                            {
                                image = srgbaFromFloatSrgb ((uint8_t*)info.ptr, numCols, numRows, info.strides[0]);
                            }
                            else
                            {
                                throw std::runtime_error("Color images must have np.uint8 or np.float32 dtype.");
                            }
                            break;
                        }

                        case 4: {
                            if (info.format == py::format_descriptor<uint8_t>::format())
                            {
                                image = ImageSRGBA((uint8_t*)info.ptr, numCols, numRows, info.strides[0], ImageSRGBA::noopReleaseFunc());
                            }
                            else if (info.format == py::format_descriptor<float>::format())
                            {
                                image = srgbaFromFloatSrgba ((uint8_t*)info.ptr, numCols, numRows, info.strides[0]);
                            }
                            else
                            {
                                throw std::runtime_error("Color images must have np.uint8 or np.float32 dtype.");
                            }
                            break;
                        }
                    }
                    break;
                }
            }

            viewer.addImageData (image, name, position, replace);
        }, py::arg("name"), py::arg("buffer"), py::arg("position") = -1, py::arg("replace") = false);

// PYTHON_VERSION_INFO comes from setup.py
#ifdef PYTHON_VERSION_INFO
    m.attr("__version__") = MACRO_STRINGIFY(PYTHON_VERSION_INFO);
#else
    m.attr("__version__") = "dev";
#endif
}
