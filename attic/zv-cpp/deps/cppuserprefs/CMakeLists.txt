cmake_minimum_required(VERSION 3.16)

project (cppuserprefs)

include_directories(${CMAKE_SOURCE_DIR})

set(CMAKE_CXX_STANDARD 17)

set(cppuserprefs_SOURCES
    CppUserPrefs.h
    tortellini.hh
)

# Can't believe I had to go through that, but test aren't built automatically by cmake,
# so we need a special dependency hack. See https://stackoverflow.com/a/10824578
function (add_test_with_deps)
    add_test (NAME "${ARGV0}_BUILD" COMMAND "${CMAKE_COMMAND}" --build ${CMAKE_BINARY_DIR} --target "${ARGV0}" "--config" "$<CONFIG>" )
    add_test ("${ARGV0}" "${ARGV0}")
    add_executable("${ARGV0}" 
        "tests/${ARGV0}.cpp" 
        ${cppuserprefs_SOURCES}
    )
    if (APPLE)
        set_source_files_properties("tests/${ARGV0}.cpp" PROPERTIES COMPILE_FLAGS "-x objective-c++")
    endif()
    set_tests_properties("${ARGV0}" PROPERTIES DEPENDS "${ARGV0}_BUILD")
    # Common lib for all tests.
    if (APPLE)
        target_link_libraries("${ARGV0}" "-framework Foundation")
    endif()
endfunction()

add_test_with_deps (test_cppuserprefs)
enable_testing()
