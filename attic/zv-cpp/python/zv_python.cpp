#include <libzv/App.h>
#include <libzv/Viewer.h>
#include <libzv/ColorConversion.h>
#include <libzv/ImageList.h>

#include <client/zv/Client.h>

#include <nanobind/nanobind.h>
#include <nanobind/stl/string.h>
#include <nanobind/stl/vector.h>
#include <nanobind/stl/function.h>
#include <nanobind/ndarray.h>

#include <imgui/imgui.h>

using namespace zv;

#define STRINGIFY(x) #x
#define MACRO_STRINGIFY(x) STRINGIFY(x)

namespace nb = nanobind;
using namespace nb::literals;

void register_App (nb::module_& m)
{
    nb::class_<App>(m, "App")
        .def(nb::init<>())

        .def("initialize", [](App& app, const std::vector<std::string>& argv) {
            return app.initialize (argv);
        }, "argv"_a = std::vector<std::string>({"zv"}))

        .def_prop_ro("numViewers", &App::numViewers)

        // rv_policy::reference_internal is required for those,
        // since the returned objects are still owned by the app.

        // The viewer is only guaranteed to stay alive until the next
        // call to updateOnce.
        .def("getViewer", [](App& app, const std::string& name) {
                return app.getViewer (name);
            }, "name"_a = "default",
            nb::rv_policy::reference_internal)

        // The viewer is only guaranteed to stay alive until the next
        // call to updateOnce.
        .def("createViewer", &App::createViewer,
            nb::rv_policy::reference_internal)

        .def("removeViewer", &App::removeViewer)

        .def("updateOnce", [](App& app, double minDuration) {
            app.updateOnce(minDuration);
        }, "minDuration"_a = 0.0);
}

ImageSRGBA imageFromPythonArray (nb::ndarray<> buffer)
{
    ImageSRGBA image;

    size_t ndim = buffer.ndim();
    if (ndim != 2 && ndim != 3)
        throw std::runtime_error("Image dimension must be 2 (grayscale) or 3 (color)");

    // Check for C-contiguous layout
    if (buffer.stride(0) < 0 || (ndim == 3 && buffer.stride(1) < 0))
    {
        throw std::runtime_error("Input image must be contiguous and c_style. You might want to use np.ascontiguousarray().");
    }

    // (H,W,1) is the same as (H,W), treat it as grayscale.
    size_t actual_dims = ndim;
    if (ndim == 3 && buffer.shape(2) == 1)
        actual_dims = 2;

    const int numRows = static_cast<int>(buffer.shape(0));
    const int numCols = static_cast<int>(buffer.shape(1));

    // Get dtype
    nb::dlpack::dtype dtype = buffer.dtype();

    switch (actual_dims)
    {
    case 2:
    {
        if (dtype.code == (uint8_t)nb::dlpack::dtype_code::UInt && dtype.bits == 8)
        {
            image = srgbaFromGray((uint8_t *)buffer.data(), numCols, numRows, buffer.stride(0));
        }
        else if (dtype.code == (uint8_t)nb::dlpack::dtype_code::Float && dtype.bits == 32)
        {
            image = srgbaFromFloatGray((uint8_t *)buffer.data(), numCols, numRows, buffer.stride(0));
        }
        else
        {
            throw std::runtime_error("Grayscale images must have np.uint8 or np.float32 dtype.");
        }
        break;
    }

    case 3:
    {
        const int numChannels = static_cast<int>(buffer.shape(2));
        if (numChannels != 3 && numChannels != 4)
            throw std::runtime_error("Channel size must be 3 (RGB) or 4 (RGBA)");

        switch (numChannels)
        {
        case 3:
        {
            if (dtype.code == (uint8_t)nb::dlpack::dtype_code::UInt && dtype.bits == 8)
            {
                image = srgbaFromSrgb((uint8_t *)buffer.data(), numCols, numRows, buffer.stride(0));
            }
            else if (dtype.code == (uint8_t)nb::dlpack::dtype_code::Float && dtype.bits == 32)
            {
                image = srgbaFromFloatSrgb((uint8_t *)buffer.data(), numCols, numRows, buffer.stride(0));
            }
            else
            {
                throw std::runtime_error("Color images must have np.uint8 or np.float32 dtype.");
            }
            break;
        }

        case 4:
        {
            if (dtype.code == (uint8_t)nb::dlpack::dtype_code::UInt && dtype.bits == 8)
            {
                image = ImageSRGBA((uint8_t *)buffer.data(), numCols, numRows, buffer.stride(0), ImageSRGBA::noopReleaseFunc());
            }
            else if (dtype.code == (uint8_t)nb::dlpack::dtype_code::Float && dtype.bits == 32)
            {
                image = srgbaFromFloatSrgba((uint8_t *)buffer.data(), numCols, numRows, buffer.stride(0));
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

void register_Viewer (nb::module_& m)
{
    nb::class_<ImageItem>(m, "ImageItem")
        .def_prop_ro("sourceImagePath", [](const ImageItem& self) { return self.sourceImagePath; })
        .def_prop_ro("prettyName", [](const ImageItem& self) { return self.prettyName; });

    nb::class_<Viewer>(m, "Viewer")
        .def_prop_ro("selectedImage", &Viewer::selectedImage)

        .def("addImageFromFile", &Viewer::addImageFromFile)

        .def("addImage", [](Viewer& viewer, const std::string& name, nb::ndarray<> buffer, int position, bool replace) {
            ImageSRGBA im = imageFromPythonArray (buffer);
            if (im.hasData())
                return viewer.addImageData (im, name, position, replace);
            return int64_t(-1);
        }, "name"_a, "buffer"_a, "position"_a = -1, "replace"_a = true)

        .def("getImageItem", &Viewer::getImageItem)

        // using EventCallbackType = std::function<void(ImageId, float, float, void* userData)>;
        // void setEventCallback (ImageId imageId, EventCallbackType callback, void* userData);
        .def("setEventCallback", &Viewer::setEventCallback)
        .def("setGlobalEventCallback", &Viewer::setGlobalEventCallback)

        .def("setLayout", &Viewer::setLayout)
        .def("runAction", [](Viewer& viewer, ImageWindowAction::Kind kind) {
            viewer.runAction (ImageWindowAction(kind));
        });

    nb::enum_<ImageWindowAction::Kind>(m, "ImageWindowAction")
        .value ("Zoom_Normal", ImageWindowAction::Kind::Zoom_Normal)
        .value ("Zoom_RestoreAspectRatio", ImageWindowAction::Kind::Zoom_RestoreAspectRatio)
        .value ("Zoom_x2", ImageWindowAction::Kind::Zoom_x2)
        .value ("Zoom_div2", ImageWindowAction::Kind::Zoom_div2)
        .value ("Zoom_Inc10p", ImageWindowAction::Kind::Zoom_Inc10p)
        .value ("Zoom_Dec10p", ImageWindowAction::Kind::Zoom_Dec10p)
        .value ("Zoom_Maxspect", ImageWindowAction::Kind::Zoom_Maxspect)
        .value ("File_OpenImage", ImageWindowAction::Kind::File_OpenImage)
        .value ("File_SaveImage", ImageWindowAction::Kind::File_SaveImage)
        .value ("View_ToggleOverlay", ImageWindowAction::Kind::View_ToggleOverlay)
        .value ("View_NextImage", ImageWindowAction::Kind::View_NextImage)
        .value ("View_PrevImage", ImageWindowAction::Kind::View_PrevImage);
}

void register_ImGui (nb::module_& zv_module)
{
    nb::module_ m = zv_module.def_submodule("imgui", "zv GUI submodule.");

    nb::enum_<ImGuiMouseButton_>(m, "MouseButton")
        .value ("Left", ImGuiMouseButton_Left)
        .value ("Right", ImGuiMouseButton_Right)
        .value ("Middle", ImGuiMouseButton_Middle);

    m.def ("IsMouseDown", &ImGui::IsMouseDown);
    m.def ("IsMouseClicked", &ImGui::IsMouseClicked);
    m.def ("IsKeyDown", &ImGui::IsKeyDown);
    m.def ("IsKeyPressed", &ImGui::IsKeyPressed);

    nb::enum_<ImGuiKey_>(m, "Key")
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

void register_Client (nb::module_& m)
{
    nb::class_<Client>(m, "Client")
        .def(nb::init<>())
        .def ("connect", &Client::connect)
        .def_prop_ro("connected", &Client::isConnected)
        .def("waitUntilDisconnected", &Client::waitUntilDisconnected)
        .def("disconnect", &Client::disconnect)
        .def("addImage", [](Client& client, const std::string& name, nb::ndarray<> buffer, const std::string& viewerName) {
            ImageSRGBA im = imageFromPythonArray (buffer);
            ClientImageBuffer clientBuffer (im.rawBytes(), im.width(), im.height(), im.bytesPerRow());
            if (!im.hasData())
                return;
            client.addImage (client.nextUniqueId(), name, clientBuffer, true /* replace */, viewerName);
        });
}

NB_MODULE(_zv, m) {
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
