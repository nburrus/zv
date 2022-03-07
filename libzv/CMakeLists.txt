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

if (APPLE)
    set (zv_platform_specific_sources 
         PlatformSpecific_macOS.cpp
         ${DEPS_DIR}/nativefiledialog-extended/src/nfd_cocoa.m
    )
elseif (UNIX)
    set (zv_platform_specific_sources
        PlatformSpecific_linux.cpp
        # Don't want to rely on gtk on Linux, breaks the standalone binary approach.
        # ${DEPS_DIR}/nativefiledialog-extended/src/nfd_gtk.cpp
    )
elseif (WIN32)
    set (zv_platform_specific_sources 
        PlatformSpecific_windows.cpp
        ${DEPS_DIR}/nativefiledialog-extended/src/nfd_win.cpp
    )
endif()

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

    # Binary resource.
    # zvIcon_xxd.cpp

    ${zv_deps_sources}
    ${zv_platform_specific_sources}
)

add_dependencies(zv glfw-deps clip-deps)

target_include_directories(zv PUBLIC
    ${DEPS_DIR}/fonts
)

target_link_libraries(zv
    glfw3
)
