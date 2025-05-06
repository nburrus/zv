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

    ${DEPS_DIR}/stb/stb_impl.cpp

    ${DEPS_DIR}/ImGuiFileDialog/ImGuiFileDialog.cpp

    ../client/zv/znet.cpp
)

if (NOT EMSCRIPTEN)
    list(APPEND zv_deps_sources
        ${DEPS_DIR}/gl3w/GL/gl3w.c
        ${DEPS_DIR}/gl3w/GL/gl3w.h
    )
endif()

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
    Annotations.h
    Annotations.cpp
    App.cpp
    App.h
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
    ImageWindowActions.h
    ImageWindowState.h
    ImageWindowState.cpp
    ImguiCanvasContainer.cpp
    ImguiCanvasContainer.h
    ImguiGLFWWindow.cpp
    ImguiGLFWWindow.h
    ImguiUtils.cpp
    ImguiUtils.h    
    ImguiWindowContainer.cpp
    ImguiWindowContainer.h
    InteractiveTool.h
    InteractiveTool.cpp
    MathUtils.h
    Modifiers.h
    Modifiers.cpp
    OpenGL.cpp
    OpenGL.h
    OpenGL_Shaders.cpp
    OpenGL_Shaders.h
    Platform.h
    PlatformSpecific.h
    Prefs.cpp
    Prefs.h
    ProggyVector_font.hpp
    Server.cpp
    Server.h
    Utils.cpp
    Utils.h
    Viewer.cpp
    Viewer.h

    # Binary resource.
    # zvIcon_xxd.cpp

    ${zv_deps_sources}
    ${zv_platform_specific_sources}
)

if (NOT EMSCRIPTEN)
    add_dependencies(zv glfw-deps clip-deps libjpeg-turbo-deps)
endif()

target_include_directories(zv PUBLIC
    ${DEPS_DIR}/fonts
)

target_include_directories(zv PRIVATE
     ${DEPS_DIR}/cppuserprefs
)

if (USE_NFD)
    target_include_directories(zv PRIVATE ${DEPS_DIR}/nativefiledialog-extended/src/include)
endif()

if (NOT EMSCRIPTEN)
    target_link_libraries(zv
        glfw3
        clip
        libjpeg-turbo
    )
endif()

if (APPLE)
    target_link_libraries(zv
        "-framework Cocoa"
        "-framework OpenGL"
        "-framework AppKit"
        "-framework CoreData"
        "-framework IOKit"
        "-framework Foundation"
    )
elseif (UNIX AND NOT EMSCRIPTEN)
    target_link_libraries(zv
        png
        GL
        dl # for gl3w
        pthread
        xcb # for clip. FIXME: should use a find_library
        X11
    )
elseif (EMSCRIPTEN)
    target_link_libraries(zv
        glfw clip
    )    
endif()

if (APPLE)
    target_compile_options(zv PUBLIC -x objective-c++)
endif()

if (UNIX)
    target_compile_options(zv PRIVATE -fPIC)
endif()