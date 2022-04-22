#include <libzv/App.h>
#include <libzv/Viewer.h>
#include <libzv/ColorConversion.h>

#include <client/Client.h>

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

ImageSRGBA imageFromPythonArray (py::array buffer)
{
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
            image = srgbaFromGray((uint8_t *)info.ptr, numCols, numRows, info.strides[0]);
        }
        else if (info.format == py::format_descriptor<float>::format())
        {
            image = srgbaFromFloatGray((uint8_t *)info.ptr, numCols, numRows, info.strides[0]);
        }
        else
        {
            throw std::runtime_error("Grayscale images must have np.uint8 or np.float32 dtype.");
        }
        break;
    }

    case 3:
    {
        const int numChannels = info.shape[2];
        if (numChannels != 3 && numChannels != 4)
            throw std::runtime_error("Channel size must be 3 (RGB) or 4 (RGBA)");

        switch (numChannels)
        {
        case 3:
        {
            if (info.format == py::format_descriptor<uint8_t>::format())
            {
                image = srgbaFromSrgb((uint8_t *)info.ptr, numCols, numRows, info.strides[0]);
            }
            else if (info.format == py::format_descriptor<float>::format())
            {
                image = srgbaFromFloatSrgb((uint8_t *)info.ptr, numCols, numRows, info.strides[0]);
            }
            else
            {
                throw std::runtime_error("Color images must have np.uint8 or np.float32 dtype.");
            }
            break;
        }

        case 4:
        {
            if (info.format == py::format_descriptor<uint8_t>::format())
            {
                image = ImageSRGBA((uint8_t *)info.ptr, numCols, numRows, info.strides[0], ImageSRGBA::noopReleaseFunc());
            }
            else if (info.format == py::format_descriptor<float>::format())
            {
                image = srgbaFromFloatSrgba((uint8_t *)info.ptr, numCols, numRows, info.strides[0]);
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
    return image;
}

void register_Viewer (py::module& m)
{
    py::class_<Viewer>(m, "Viewer")
        .def_property_readonly("selectedImage", &Viewer::selectedImage)
        
        .def("addImageFromFile", &Viewer::addImageFromFile)

        .def("addImage", [](Viewer& viewer, const std::string& name, py::array buffer, int position, bool replace) {
            ImageSRGBA im = imageFromPythonArray (buffer);
            if (im.hasData())
                return viewer.addImageData (im, name, position, replace);
            return int64_t(-1);
        }, py::arg("name"), py::arg("buffer"), py::arg("position") = -1, py::arg("replace") = true)

        // using EventCallbackType = std::function<void(ImageId, float, float, void* userData)>;
        // void setEventCallback (ImageId imageId, EventCallbackType callback, void* userData);
        .def("setEventCallback", &Viewer::setEventCallback)

        .def("setLayout", &Viewer::setLayout)
        .def("runAction", &Viewer::runAction);

    py::enum_<ImageWindowAction::Kind>(m, "ImageWindowAction")
        .value ("Zoom_Normal", ImageWindowAction::Kind::Zoom_Normal)
        .value ("Zoom_RestoreAspectRatio", ImageWindowAction::Kind::Zoom_RestoreAspectRatio)
        .value ("Zoom_x2", ImageWindowAction::Kind::Zoom_x2)
        .value ("Zoom_div2", ImageWindowAction::Kind::Zoom_div2)
        .value ("Zoom_Inc10p", ImageWindowAction::Kind::Zoom_Inc10p)
        .value ("Zoom_Dec10p", ImageWindowAction::Kind::Zoom_Dec10p)
        .value ("Zoom_Maxspect", ImageWindowAction::Kind::Zoom_Maxspect)        
        .value ("File_OpenImage", ImageWindowAction::Kind::File_OpenImage)
        .value ("View_ToggleOverlay", ImageWindowAction::Kind::View_ToggleOverlay)
        .value ("View_NextImage", ImageWindowAction::Kind::View_NextImage)
        .value ("View_PrevImage", ImageWindowAction::Kind::View_PrevImage);
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
    m.def ("IsKeyDown", &ImGui::IsKeyDown);

    py::enum_<ImGuiKey_>(m, "Key")
        .value("Tab", ImGuiKey_Tab)
        .value("LeftArrow", ImGuiKey_LeftArrow)
        .value("RightArrow", ImGuiKey_RightArrow)
        .value("UpArrow", ImGuiKey_UpArrow)
        .value("DownArrow", ImGuiKey_DownArrow)
        .value("PageUp", ImGuiKey_PageUp)
        .value("PageDown", ImGuiKey_PageDown)
        .value("Home", ImGuiKey_Home)
        .value("End", ImGuiKey_End)
        .value("Insert", ImGuiKey_Insert)
        .value("Delete", ImGuiKey_Delete)
        .value("Backspace", ImGuiKey_Backspace)
        .value("Space", ImGuiKey_Space)
        .value("Enter", ImGuiKey_Enter)
        .value("Escape", ImGuiKey_Escape)
        .value("LeftCtrl", ImGuiKey_LeftCtrl) .value("LeftShift", ImGuiKey_LeftShift) .value("LeftAlt", ImGuiKey_LeftAlt) .value("LeftSuper", ImGuiKey_LeftSuper)
        .value("RightCtrl", ImGuiKey_RightCtrl) .value("RightShift", ImGuiKey_RightShift) .value("RightAlt", ImGuiKey_RightAlt) .value("RightSuper", ImGuiKey_RightSuper)
        .value("Menu", ImGuiKey_Menu)
        .value("0", ImGuiKey_0) .value("1", ImGuiKey_1) .value("2", ImGuiKey_2) .value("3", ImGuiKey_3) .value("4", ImGuiKey_4) .value("5", ImGuiKey_5) .value("6", ImGuiKey_6) .value("7", ImGuiKey_7) .value("8", ImGuiKey_8) .value("9", ImGuiKey_9)
        .value("A", ImGuiKey_A) .value("B", ImGuiKey_B) .value("C", ImGuiKey_C) .value("D", ImGuiKey_D) .value("E", ImGuiKey_E) .value("F", ImGuiKey_F) .value("G", ImGuiKey_G) .value("H", ImGuiKey_H) .value("I", ImGuiKey_I) .value("J", ImGuiKey_J)
        .value("K", ImGuiKey_K) .value("L", ImGuiKey_L) .value("M", ImGuiKey_M) .value("N", ImGuiKey_N) .value("O", ImGuiKey_O) .value("P", ImGuiKey_P) .value("Q", ImGuiKey_Q) .value("R", ImGuiKey_R) .value("S", ImGuiKey_S) .value("T", ImGuiKey_T)
        .value("U", ImGuiKey_U) .value("V", ImGuiKey_V) .value("W", ImGuiKey_W) .value("X", ImGuiKey_X) .value("Y", ImGuiKey_Y) .value("Z", ImGuiKey_Z)
        .value("F1", ImGuiKey_F1) .value("F2", ImGuiKey_F2) .value("F3", ImGuiKey_F3) .value("F4", ImGuiKey_F4) .value("F5", ImGuiKey_F5) .value("F6", ImGuiKey_F6)
        .value("F7", ImGuiKey_F7) .value("F8", ImGuiKey_F8) .value("F9", ImGuiKey_F9) .value("F10", ImGuiKey_F10) .value("F11", ImGuiKey_F11) .value("F12", ImGuiKey_F12)
        .value("Apostrophe", ImGuiKey_Apostrophe)   
        .value("Comma", ImGuiKey_Comma)        
        .value("Minus", ImGuiKey_Minus)        
        .value("Period", ImGuiKey_Period)       
        .value("Slash", ImGuiKey_Slash)        
        .value("Semicolon", ImGuiKey_Semicolon)    
        .value("Equal", ImGuiKey_Equal)        
        .value("LeftBracket", ImGuiKey_LeftBracket)  
        .value("Backslash", ImGuiKey_Backslash)    
        .value("RightBracket", ImGuiKey_RightBracket) 
        .value("GraveAccent", ImGuiKey_GraveAccent)  
        .value("CapsLock", ImGuiKey_CapsLock)
        .value("ScrollLock", ImGuiKey_ScrollLock)
        .value("NumLock", ImGuiKey_NumLock)
        .value("PrintScreen", ImGuiKey_PrintScreen)
        .value("Pause", ImGuiKey_Pause)
        .value("Keypad0", ImGuiKey_Keypad0) .value("Keypad1", ImGuiKey_Keypad1) .value("Keypad2", ImGuiKey_Keypad2) .value("Keypad3", ImGuiKey_Keypad3) .value("Keypad4", ImGuiKey_Keypad4)
        .value("Keypad5", ImGuiKey_Keypad5) .value("Keypad6", ImGuiKey_Keypad6) .value("Keypad7", ImGuiKey_Keypad7) .value("Keypad8", ImGuiKey_Keypad8) .value("Keypad9", ImGuiKey_Keypad9)
        .value("KeypadDecimal", ImGuiKey_KeypadDecimal)
        .value("KeypadDivide", ImGuiKey_KeypadDivide)
        .value("KeypadMultiply", ImGuiKey_KeypadMultiply)
        .value("KeypadSubtract", ImGuiKey_KeypadSubtract)
        .value("KeypadAdd", ImGuiKey_KeypadAdd)
        .value("KeypadEnter", ImGuiKey_KeypadEnter)
        .value("KeypadEqual", ImGuiKey_KeypadEqual);
}

void register_Client (py::module& m)
{
    py::class_<Client>(m, "Client")
        .def(py::init<>())
        .def ("connect", &Client::connect)
        .def_property_readonly("connected", &Client::isConnected)
        .def("waitUntilDisconnected", &Client::waitUntilDisconnected)
        .def("disconnect", &Client::disconnect)
        .def("addImage", [](Client& client, const std::string& name, py::array buffer, const std::string& viewerName) {
            ImageSRGBA im = imageFromPythonArray (buffer);
            ClientImageBuffer clientBuffer (im.rawBytes(), im.width(), im.height(), im.bytesPerRow());
            if (!im.hasData())
                return;
            client.addImage (client.nextUniqueId(), name, clientBuffer, true /* replace */, viewerName);
        });
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
    register_Client (m);

// PYTHON_VERSION_INFO comes from setup.py
#ifdef PYTHON_VERSION_INFO
    m.attr("__version__") = MACRO_STRINGIFY(PYTHON_VERSION_INFO);
#else
    m.attr("__version__") = "dev";
#endif
}
