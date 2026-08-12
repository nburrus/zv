# Process

1. Use https://icomoon.io/app/#/select to generate a ttf . Import the project from the json in zv-fonts-v1.0.zip .
2. Add the icons you want.
3. Add the new unicode values to `FontIcomoon.h`. To convert from UTF16 to a string of UTF8, use https://www.coderstool.com/unicode-text-converter . Type e.g. `\ue900` in the UTF-16 section and grab the corresponding hex UTF-8 sequence.
4. Use deps/imgui/misc/fonts/binary_to_compressed_c.cpp to generate the C code for the ttf:
`binary_to_compressed_c icomoon/fonts/zv-fonts.ttf Icomoon`
5. Put that string into `FontIcomoon_data.hpp`.

# More refs:
- https://github.com/ocornut/imgui/blob/master/docs/FONTS.md#using-custom-colorful-icons
- https://github.com/wolfpld/tracy/tree/master/profiler/src



