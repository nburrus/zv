#pragma once

#include <string>
#include <memory>

namespace zv {

class WebClipboard {
public:
    static bool copyTextToClipboard(const std::string& text);
    static bool copyImageToClipboard(const void* imageData, int width, int height);
    static bool pasteFromClipboard(void** imageData, int* width, int* height);
};

} // namespace zv 