# GLFW
git subtree add --prefix deps/glfw https://github.com/glfw/glfw.git master --squash

# ImGui
git subtree add --prefix deps/imgui https://github.com/ocornut/imgui.git master --squash

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

# pybind11
git subtree add --prefix deps/pybind11 git@github.com:pybind/pybind11.git stable --squash

# libjpeg-turbo

Imported from https://github.com/libjpeg-turbo/libjpeg-turbo.git 

Copied and removed the testimages + unused simd architectures.

Last update: Nov 23, 2022 commit 74d5b168f7a00250c1dc0001527d10175e00b779 .
