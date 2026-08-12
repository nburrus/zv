# GLFW
git subtree add --prefix deps/glfw https://github.com/glfw/glfw.git master --squash

# ImGui
git subtree add --prefix deps/imgui https://github.com/ocornut/imgui.git master --squash

# ImPlot
git subtree add --prefix deps/implot https://github.com/epezent/implot.git v0.17 --squash

# gl3w
deps/gl3w taken from Imgui 1.83, before they removed it.

# Nativefiledialog
git subtree add --prefix deps/nativefiledialog-extended git@github.com:btzy/nativefiledialog-extended.git master --squash

# ImGui file dialog (for Linux)
git subtree add --prefix deps/ImGuiFileDialog git@github.com:aiekick/ImGuiFileDialog.git Lib_Only --squash

# Clip
git subtree add --prefix deps/clip git@github.com:dacap/clip.git main --squash
Tweaked to add an install target.

# CppUserPrefs
git subtree add --prefix deps/cppuserprefs git@github.com:nburrus/cppuserprefs.git main --squash

# libjpeg-turbo

Imported from https://github.com/libjpeg-turbo/libjpeg-turbo.git 

Copied and removed the testimages + unused simd architectures.

Last update: Nov 23, 2022 commit 74d5b168f7a00250c1dc0001527d10175e00b779 .

# doctest

Header-only testing framework from https://github.com/doctest/doctest.git

Only the single header file `doctest.h` is included in `deps/doctest/`.

# nanobind

Python bindings library from https://github.com/wjakob/nanobind

NOT vendored in this repository. Installed via pip as a Python package dependency.
Used for generating the Python bindings in the `python/` directory.
