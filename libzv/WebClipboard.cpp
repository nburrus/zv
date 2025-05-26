#include "WebClipboard.h"

#if PLATFORM_EMSCRIPTEN
#include <emscripten.h>
#include <emscripten/val.h>
#endif

namespace zv {

bool WebClipboard::copyTextToClipboard(const std::string& text) {
#if PLATFORM_EMSCRIPTEN
    emscripten::val::global("js_copyTextToClipboard").call<bool>("call", emscripten::val::null(), text);
    return true;
#else
    return false;
#endif
}

bool WebClipboard::copyImageToClipboard(const void* imageData, int width, int height) {
#if PLATFORM_EMSCRIPTEN
    // Create a temporary canvas to hold the image data
    emscripten::val document = emscripten::val::global("document");
    emscripten::val canvas = document.call<emscripten::val>("createElement", std::string("canvas"));
    canvas.set("width", width);
    canvas.set("height", height);
    
    // Get the 2D context and put the image data
    emscripten::val ctx = canvas.call<emscripten::val>("getContext", std::string("2d"));
    emscripten::val imageDataVal = emscripten::val::module_property("ImageData").new_(
        emscripten::val(emscripten::typed_memory_view(width * height * 4, static_cast<const uint8_t*>(imageData))),
        width,
        height
    );
    ctx.call<void>("putImageData", imageDataVal, 0, 0);
    
    // Copy to clipboard
    emscripten::val::global("js_copyImageToClipboard").call<bool>("call", emscripten::val::null(), canvas);
    return true;
#else
    return false;
#endif
}

bool WebClipboard::pasteFromClipboard(void** imageData, int* width, int* height) {
#if PLATFORM_EMSCRIPTEN
    emscripten::val result = emscripten::val::global("js_pasteFromClipboard").call<emscripten::val>("call", emscripten::val::null());
    if (result.isNull()) {
        return false;
    }
    
    // Get the image data from the canvas
    emscripten::val ctx = result.call<emscripten::val>("getContext", std::string("2d"));
    emscripten::val imageDataVal = ctx.call<emscripten::val>("getImageData", 0, 0, result["width"], result["height"]);
    
    // Copy the data to the output buffer
    *width = result["width"].as<int>();
    *height = result["height"].as<int>();
    size_t dataSize = *width * *height * 4;
    *imageData = malloc(dataSize);
    emscripten::val dataView = emscripten::val::global("Uint8Array").new_(imageDataVal["data"]);
    dataView.call<void>("forEach", emscripten::val::global("Function").new_(
        std::string("(v, i) => { HEAP8[this + i] = v; }"),
        emscripten::val(reinterpret_cast<intptr_t>(*imageData))
    ));
    return true;
#else
    return false;
#endif
}

} // namespace zv 