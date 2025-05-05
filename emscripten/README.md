# Steps to build

brew install emscripten
mkdir build-emscripten
cd build-emscripten
emcmake cmake .. -G Ninja
ninja zvbin

# Next steps

- Add a runloop with emscripten_set_main_loop , right now it doesn't show anything
- Make it so the controls window is not a separate GLFW window, but a separate imgui window
- Avoid creating multiple zv windows entirely, we can only have one glfwCreateWindow
