cmake_minimum_required(VERSION 3.5.1)

project(zv)
set(PROJECT_VERSION_MAJOR 0)
set(PROJECT_VERSION_MINOR 1)
set(PROJECT_VERSION ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR})

# Get the current commit as a version variable.
# Make sure that we'll re-run configure if the commit changes.
# https://cmake.org/pipermail/cmake/2018-October/068389.html
execute_process(COMMAND git -C "${CMAKE_SOURCE_DIR}" rev-parse --short HEAD
         RESULT_VARIABLE PROJECT_VERSION_COMMIT_RESULT
         OUTPUT_VARIABLE PROJECT_VERSION_COMMIT
         OUTPUT_STRIP_TRAILING_WHITESPACE)
set_property(GLOBAL APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/.git/index")

include(ExternalProject)

configure_file(${CMAKE_SOURCE_DIR}/libzv/GeneratedConfig.h.in ${PROJECT_BINARY_DIR}/GeneratedConfig.h)
include_directories(${PROJECT_BINARY_DIR})

set(PRECOMPILED_FOLDER "${CMAKE_SOURCE_DIR}/deps/prebuilt/${CMAKE_SYSTEM_NAME}")

# Hack to accept multiple archs separated by semicolon.
# https://discourse.cmake.org/t/how-to-pass-cmake-osx-architectures-to-externalproject-add/2262/3
set(semicolon_smuggle "-+-")
string(REPLACE ";" "${semicolon_smuggle}" cmake_osx_archs_special_separator "${CMAKE_OSX_ARCHITECTURES}")

# DebugFast does not exist for the deps, so need to set it to Debug.
if (CMAKE_BUILD_TYPE STREQUAL DebugFast OR CMAKE_BUILD_TYPE STREQUAL DEBUGFAST)
  set (CMAKE_DEPS_CONFIG Debug)
else()
  set (CMAKE_DEPS_CONFIG ${CMAKE_BUILD_TYPE})
endif()

ExternalProject_Add(glfw-deps
  LIST_SEPARATOR "${semicolon_smuggle}"
  PREFIX glfw
  SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/deps/glfw"
  CMAKE_ARGS
        -DCMAKE_BUILD_TYPE=${CMAKE_DEPS_CONFIG}
        -DCMAKE_OSX_ARCHITECTURES=${cmake_osx_archs_special_separator}
        -DCMAKE_INSTALL_PREFIX:PATH=${PRECOMPILED_FOLDER}/glfw
        -DGLFW_BUILD_DOCS=OFF
        -DGLFW_BUILD_EXAMPLES=OFF
        -DGLFW_BUILD_TESTS=OFF
)

set(GLFW_LIBRARY_FILENAME "${CMAKE_STATIC_LIBRARY_PREFIX}glfw3${CMAKE_STATIC_LIBRARY_SUFFIX}")
add_library(glfw3 STATIC IMPORTED)
set_target_properties(glfw3 PROPERTIES IMPORTED_LOCATION ${PRECOMPILED_FOLDER}/glfw/lib/${GLFW_LIBRARY_FILENAME})
include_directories(glfw3 PROPERTIES IMPORTED_LOCATION ${PRECOMPILED_FOLDER}/glfw/include)

if (WIN32)
  target_link_libraries(glfw3 INTERFACE opengl32)
endif()

if (UNIX)
  set(PYTHON_EXTRA_CMAKE_CXX_FLAGS "-fPIC")
endif()

if (UNIX AND NOT APPLE)
  set(CLIP_X11_WITH_PNG 1)
endif()
set(CLIP_LIBRARY_FILENAME "${CMAKE_STATIC_LIBRARY_PREFIX}clip${CMAKE_STATIC_LIBRARY_SUFFIX}")
ExternalProject_Add(clip-deps
  LIST_SEPARATOR "${semicolon_smuggle}"
  PREFIX clip
  SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/deps/clip"
  CMAKE_ARGS
        -DCMAKE_BUILD_TYPE=${CMAKE_DEPS_CONFIG}
        -DCMAKE_OSX_ARCHITECTURES=${cmake_osx_archs_special_separator}
        -DCMAKE_INSTALL_PREFIX:PATH=${PRECOMPILED_FOLDER}/clip
        -DCMAKE_CXX_FLAGS=${PYTHON_EXTRA_CMAKE_CXX_FLAGS}
        -DCLIP_EXAMPLES=OFF
        -DCLIP_TESTS=OFF
        -DCLIP_X11_WITH_PNG=${CLIP_X11_WITH_PNG}
  # Now handled in deps/clip/CMakeLists.txt, otherwise this leads
  # to issues with Visual Studio 2019 as it cannot find an INSTALL
  # target in the external project.
  # INSTALL_COMMAND 
  #      cmake -E make_directory "${PRECOMPILED_FOLDER}/clip/lib/"
  #      COMMAND cmake -E copy "<BINARY_DIR>/${CLIP_LIBRARY_FILENAME}" "${PRECOMPILED_FOLDER}/clip/lib/"
)

add_library(clip STATIC IMPORTED)
set_target_properties(clip PROPERTIES IMPORTED_LOCATION ${PRECOMPILED_FOLDER}/clip/lib/${CLIP_LIBRARY_FILENAME})
include_directories(clip PROPERTIES IMPORTED_LOCATION ${CMAKE_CURRENT_SOURCE_DIR}/deps)
if (WIN32)
  target_link_libraries(clip INTERFACE Shlwapi)
endif()

# Specify a default value, for some reason libjpeg-turbo can't find it in the PATH.
if (NOT CMAKE_ASM_NASM_COMPILER)
  if (WIN32)
    set (CMAKE_ASM_NASM_COMPILER "C:/Program Files/NASM/nasm.exe")
  elseif (APPLE)
    set (CMAKE_ASM_NASM_COMPILER "/usr/bin/nasm")
  else()
    set (CMAKE_ASM_NASM_COMPILER "/usr/bin/nasm")
  endif()
endif()

ExternalProject_Add(libjpeg-turbo-deps
  LIST_SEPARATOR "${semicolon_smuggle}"
  PREFIX libjpeg-turbo
  SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/deps/libjpeg-turbo"
  CMAKE_ARGS
        -DCMAKE_BUILD_TYPE=${CMAKE_DEPS_CONFIG}
        -DCMAKE_OSX_ARCHITECTURES=${cmake_osx_archs_special_separator}
        -DCMAKE_INSTALL_PREFIX:PATH=${PRECOMPILED_FOLDER}/libjpeg-turbo
        -DCMAKE_ASM_NASM_COMPILER=${CMAKE_ASM_NASM_COMPILER}
        -DCMAKE_POSITION_INDEPENDENT_CODE=YES
)
if (WIN32)
  set(TURBOJPEG_LIBRARY_FILENAME "${CMAKE_STATIC_LIBRARY_PREFIX}turbojpeg-static${CMAKE_STATIC_LIBRARY_SUFFIX}")
else()
  set(TURBOJPEG_LIBRARY_FILENAME "${CMAKE_STATIC_LIBRARY_PREFIX}turbojpeg${CMAKE_STATIC_LIBRARY_SUFFIX}")
endif()
add_library(libjpeg-turbo STATIC IMPORTED)
set_target_properties(libjpeg-turbo PROPERTIES IMPORTED_LOCATION ${PRECOMPILED_FOLDER}/libjpeg-turbo/lib/${TURBOJPEG_LIBRARY_FILENAME})
target_include_directories(libjpeg-turbo INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/deps/libjpeg-turbo)

set(CMAKE_CXX_STANDARD 17)

# Add DebugFast settings since image processing is typically too slow to happen
# without any optimization.
set(CMAKE_CXX_FLAGS_DEBUGFAST "${CMAKE_CXX_FLAGS_DEBUG} -Os" CACHE STRING "" FORCE)
set(CMAKE_C_FLAGS_DEBUGFAST "${CMAKE_C_FLAGS_DEBUG}" CACHE STRING "" FORCE)
set(CMAKE_EXE_LINKER_FLAGS_DEBUGFAST "${CMAKE_EXE_LINKER_FLAGS_DEBUG}" CACHE STRING "" FORCE)
set(CMAKE_MODULE_LINKER_FLAGS_DEBUGFAST "${CMAKE_MODULE_LINKER_FLAGS_DEBUG}" CACHE STRING "" FORCE)
set(CMAKE_SHARED_LINKER_FLAGS_DEBUGFAST "${CMAKE_SHARED_LINKER_FLAGS_DEBUG}" CACHE STRING "" FORCE)
mark_as_advanced(
  CMAKE_CXX_FLAGS_DEBUGFAST
  CMAKE_C_FLAGS_DEBUGFAST
  CMAKE_EXE_LINKER_FLAGS_DEBUGFAST
  CMAKE_MODULE_LINKER_FLAGS_DEBUGFAST
  CMAKE_SHARED_LINKER_FLAGS_DEBUGFAST)
# Update the documentation string of CMAKE_BUILD_TYPE for GUIs

set(CMAKE_CONFIGURATION_TYPES "Debug" "Release" "RelWithDebInfo" "DebugFast")

# add_compile_options(
#     $<$<CONFIG:Debug>:-Os>
# )

set (DEPS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/deps")

set (IMGUI_DIR "${DEPS_DIR}/imgui")
include_directories(
    ${IMGUI_DIR} 
    ${IMGUI_DIR}/backends 
    ${DEPS_DIR}/gl3w
)

include_directories(${CMAKE_CURRENT_SOURCE_DIR})
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/deps/other)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/deps/stb)

if (WIN32)
  add_definitions(-D_USE_MATH_DEFINES -DZN_STATIC_API -DNOMINMAX)
endif()

add_subdirectory(libzv)
add_subdirectory(zv)
add_subdirectory(zv-client)
add_subdirectory(client)

option(BUILD_PYTHON "Build the Python module" OFF)
if (BUILD_PYTHON)
  add_subdirectory(deps/pybind11)
  add_subdirectory(python)
endif()


# enable_testing()
# add_subdirectory(tests)

