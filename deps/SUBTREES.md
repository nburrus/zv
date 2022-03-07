# GLFW
git subtree add --prefix deps/glfw https://github.com/glfw/glfw.git master --squash

# ImGui
git subtree add --prefix deps/imgui https://github.com/ocornut/imgui.git master --squash

# gl3w
deps/gl3w taken from Imgui 1.83, before they removed it.

# Nativefiledialog
git subtree add --prefix deps/nativefiledialog-extended git@github.com:btzy/nativefiledialog-extended.git master --squash

# Clip
git subtree add --prefix deps/clip git@github.com:dacap/clip.git main --squash
Tweaked to add an install target.

# CppUserPrefs
git subtree add --prefix deps/cppuserprefs git@github.com:nburrus/cppuserprefs.git main --squash
