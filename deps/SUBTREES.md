git subtree pull --prefix deps/glfw https://github.com/glfw/glfw.git master --squash
git subtree pull --prefix deps/imgui https://github.com/ocornut/imgui.git master --squash
deps/gl3w taken from Imgui 1.83, before they removed it.

git subtree add --prefix deps/nativefiledialog-extended git@github.com:btzy/nativefiledialog-extended.git master --squash

git subtree add --prefix deps/clip git@github.com:dacap/clip.git master --squash