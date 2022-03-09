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

    ${DEPS_DIR}/ImGuiFileDialog/ImGuiFileDialog.cpp
)

if (APPLE)
    set(USE_NFD 1)
    set (zv_platform_specific_sources 
         PlatformSpecific_macOS.cpp
         ${DEPS_DIR}/nativefiledialog-extended/src/nfd_cocoa.m         
    )
elseif (UNIX)
    set (zv_platform_specific_sources
        PlatformSpecific_linux.cpp
        # Don't want to rely on gtk on Linux, it would break the standalone binary approach.
        # Instead use 
        # ${DEPS_DIR}/nativefiledialog-extended/src/nfd_gtk.cpp
    )

elseif (WIN32)
    set(USE_NFD 1)
    set (zv_platform_specific_sources 
        PlatformSpecific_windows.cpp
        ${DEPS_DIR}/nativefiledialog-extended/src/nfd_win.cpp
    )
endif()

add_library(zv
    ColorConversion.cpp
    ColorConversion.h
    ControlsWindow.cpp
    ControlsWindow.h
    GLFWUtils.cpp
    GLFWUtils.h
    HelpWindow.cpp
    HelpWindow.h
    Icon_xxd.cpp
    Icon.cpp
    Icon.h
    Image_stb.cpp
    Image.h
    ImageCursorOverlay.cpp
    ImageCursorOverlay.h
    ImageList.cpp
    ImageList.h
    ImageWindow.cpp
    ImageWindow.h
    ImageWindowState.h
    ImguiGLFWWindow.cpp
    ImguiGLFWWindow.h
    ImguiUtils.h
    MathUtils.h
    OpenGL.cpp
    OpenGL.h
    OpenGL_Shaders.cpp
    OpenGL_Shaders.h
    Platform.h
    PlatformSpecific.h
    Prefs.cpp
    Prefs.h
    ProggyVector_font.hpp
    Utils.cpp
    Utils.h
    Viewer.cpp
    Viewer.h

    # Binary resource.
    # zvIcon_xxd.cpp

    ${zv_deps_sources}
    ${zv_platform_specific_sources}
)

add_dependencies(zv glfw-deps clip-deps)

target_include_directories(zv PUBLIC
    ${DEPS_DIR}/fonts
)

target_include_directories(zv PRIVATE
     ${DEPS_DIR}/cppuserprefs
)

if (USE_NFD)
    target_include_directories(zv PRIVATE ${DEPS_DIR}/nativefiledialog-extended/src/include)
endif()

target_link_libraries(zv
    glfw3
    clip
)

if (APPLE)
    target_link_libraries(zv
        "-framework Cocoa"
        "-framework OpenGL"
        "-framework AppKit"
        "-framework CoreData"
        "-framework IOKit"
        "-framework Foundation"
    )
elseif (UNIX)
    target_link_libraries(zv
        GL
        dl # for gl3w
        pthread
        xcb # for clip. FIXME: should use a find_library
        X11
    )
endif()

if (APPLE)
    target_compile_options(zv PUBLIC -x objective-c++)
endif()

if (UNIX)
    target_compile_options(zv PRIVATE -fPIC)
endif()