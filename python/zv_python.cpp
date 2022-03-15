#include <libzv/App.h>
#include <libzv/Viewer.h>
#include <libzv/ColorConversion.h>

#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <pybind11/functional.h>
#include <pybind11/numpy.h>

#include <imgui/imgui.h>

using namespace zv;

#define STRINGIFY(x) #x
#define MACRO_STRINGIFY(x) STRINGIFY(x)

namespace py = pybind11;

void register_App (py::module& m)
{
    py::class_<App>(m, "App")
        .def(py::init<>())

        .def("initialize", [](App& app, const std::vector<std::string>& argv) {
            return app.initialize (argv);
        }, py::arg("argv") = std::vector<std::string>({"zv"}))

        .def_property_readonly("numViewers", &App::numViewers)

        // return_value_policy::reference_internal) is required for those,
        // since the returned objects are still owned by the app.

        // The viewer is only guaranteed to stay alive until the next
        // call to updateOnce.
        .def("getViewer", [](App& app, const std::string& name) {
                return app.getViewer (name);
            }, py::arg("name") = "default", 
            py::return_value_policy::reference_internal)

        // The viewer is only guaranteed to stay alive until the next
        // call to updateOnce.
        .def("createViewer", &App::createViewer, 
            py::return_value_policy::reference_internal)

        .def("updateOnce", [](App& app, double minDuration) {
            app.updateOnce(minDuration);
        }, py::arg("minDuration") = 0.0);
}

void register_Viewer (py::module& m)
{
    py::class_<Viewer>(m, "Viewer")
        .def_property_readonly("selectedImage", &Viewer::selectedImage)
        
        .def("addImageFromFile", &Viewer::addImageFromFile)

        .def("addImage", [](Viewer& viewer, const std::string& name, py::array buffer, int position, bool replace) {
            /* Request a buffer descriptor from Python */
            py::buffer_info info = buffer.request();

            ImageSRGBA image;

            if (info.ndim != 2 && info.ndim != 3)
                throw std::runtime_error("Image dimension must be 2 (grayscale) or 3 (color)");

            if (!(buffer.flags() & py::array::c_style))
            {
                throw std::runtime_error("Input image must be contiguous and c_style. You might want to use np.ascontiguousarray().");
            }

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

            return viewer.addImageData (image, name, position, replace);
        }, py::arg("name"), py::arg("buffer"), py::arg("position") = -1, py::arg("replace") = false)

        // using EventCallbackType = std::function<void(ImageId, float, float, void* userData)>;
        // void setEventCallback (ImageId imageId, EventCallbackType callback, void* userData);
        .def("setEventCallback", &Viewer::setEventCallback)

        .def("setLayout", &Viewer::setLayout)
        .def("runAction", &Viewer::runAction);

    py::enum_<ImageWindowAction>(m, "ImageWindowAction")
        .value ("Zoom_Normal", ImageWindowAction::Zoom_Normal)
        .value ("Zoom_RestoreAspectRatio", ImageWindowAction::Zoom_RestoreAspectRatio)
        .value ("Zoom_x2", ImageWindowAction::Zoom_x2)
        .value ("Zoom_div2", ImageWindowAction::Zoom_div2)
        .value ("Zoom_Inc10p", ImageWindowAction::Zoom_Inc10p)
        .value ("Zoom_Dec10p", ImageWindowAction::Zoom_Dec10p)
        .value ("Zoom_Maxspect", ImageWindowAction::Zoom_Maxspect)        
        .value ("File_OpenImage", ImageWindowAction::File_OpenImage)
        .value ("View_ToggleOverlay", ImageWindowAction::View_ToggleOverlay)
        .value ("View_NextImage", ImageWindowAction::View_NextImage)
        .value ("View_PrevImage", ImageWindowAction::View_PrevImage);
}

void register_ImGui (py::module& zv_module)
{
    py::module_ m = zv_module.def_submodule("imgui", "zv GUI submodule.");
    
    py::enum_<ImGuiMouseButton_>(m, "MouseButton")
        .value ("Left", ImGuiMouseButton_Left)
        .value ("Right", ImGuiMouseButton_Right)
        .value ("Middle", ImGuiMouseButton_Middle);
    
    m.def ("IsMouseDown", &ImGui::IsMouseDown);
    m.def ("IsMouseClicked", &ImGui::IsMouseClicked);
}

PYBIND11_MODULE(_zv, m) {
    m.doc() = R"pbdoc(
        zv python module
        -----------------------
        .. currentmodule:: zv
        .. autosummary::
           :toctree: _generate
           add
    )pbdoc";

    register_App (m);
    register_Viewer (m);
    register_ImGui (m);

// PYTHON_VERSION_INFO comes from setup.py
#ifdef PYTHON_VERSION_INFO
    m.attr("__version__") = MACRO_STRINGIFY(PYTHON_VERSION_INFO);
#else
    m.attr("__version__") = "dev";
#endif
}
