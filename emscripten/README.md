# Steps to build

brew install emscripten
mkdir build-emscripten
cd build-emscripten
emcmake cmake .. -G Ninja
ninja zvbin
cd build-emscripten/zv
emrun --no-browser zv.html

# Next steps

- Support copy/pasting of images
- Support open an image from an URL
- Support open an image from a file on the host
