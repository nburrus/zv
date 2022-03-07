add_definitions(-DIMGUI_IMPL_OPENGL_LOADER_GL3W=1)

set (zv_deps_sources
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui.h
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/imgui_demo.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    
    ${IMGUI_DIR}/backends/imgui_impl_opengl3.cpp
    ${IMGUI_DIR}/backends/imgui_impl_glfw.cpp

    ${DEPS_DIR}/nativefiledialog-extended/src/include/nfd.h
    ${DEPS_DIR}/nativefiledialog-extended/src/include/nfd.hpp

    ${DEPS_DIR}/cppuserprefs/CppUserPrefs.h
    ${DEPS_DIR}/cppuserprefs/tortellini.hh

    ${DEPS_DIR}/gl3w/GL/gl3w.c
    ${DEPS_DIR}/gl3w/GL/gl3w.h
    ${DEPS_DIR}/stb/stb_impl.cpp
)

add_library(zv 
    Image.h    
    MathUtils.h
    OpenGL.h
    OpenGL.cpp
    OpenGL_Shaders.h
    OpenGL_Shaders.cpp
    Platform.h
    Utils.cpp
    Utils.h
    ${zv_deps_sources}
)

add_dependencies(zv glfw-deps clip-deps)

target_link_libraries(zv
    glfw3
)
