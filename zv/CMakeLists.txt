
if (WIN32)
    set (zvbin_RESOURCES zv.rc zv_windows.manifest)
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} /SUBSYSTEM:WINDOWS /ENTRY:mainCRTStartup")
endif()

add_executable(zvbin WIN32 main.cpp ${zvbin_RESOURCES})

# add_executable(binary_to_compressed "${DEPS_DIR}/imgui/misc/fonts/binary_to_compressed_c.cpp")

target_link_libraries(zvbin
    zv
)

if (WIN32)
    set(CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS_SKIP TRUE)
    include(InstallRequiredSystemLibraries)
    install(PROGRAMS ${CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS}
            DESTINATION .
            COMPONENT applications)

    install(TARGETS zv 
         DESTINATION .
         COMPONENT applications
    )
    
    set(CPACK_PACKAGE_NAME "zv")
    set(CPACK_PACKAGE_VENDOR "Nicolas Burrus")
    set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "zv is a lightweight image viewer, inspired by xv.")
    set(CPACK_PACKAGE_VERSION_MAJOR ${PROJECT_VERSION_MAJOR})
    set(CPACK_PACKAGE_VERSION_MINOR ${PROJECT_VERSION_MINOR})
    set(CPACK_PACKAGE_VERSION_PATCH ${PROJECT_VERSION_PATCH})
    if ("${PROJECT_VERSION_PATCH}" STREQUAL "")
        set(CPACK_PACKAGE_VERSION "${CPACK_PACKAGE_VERSION_MAJOR}.${CPACK_PACKAGE_VERSION_MINOR}")
    else()
        set(CPACK_PACKAGE_VERSION "${CPACK_PACKAGE_VERSION_MAJOR}.${CPACK_PACKAGE_VERSION_MINOR}.${CPACK_PACKAGE_VERSION_PATCH}")
    endif()
    set(CPACK_PACKAGE_INSTALL_DIRECTORY "zv")
    
    set(CPACK_COMPONENT_APPLICATIONS_DISPLAY_NAME "zv")
    set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_SOURCE_DIR}/LICENSE")
    
    set(CPACK_PACKAGE_ICON "${CMAKE_SOURCE_DIR}\\\\zv\\\\zv.ico")
    set(CPACK_NSIS_MUI_ICON "${CMAKE_SOURCE_DIR}/zv/zv.ico")
    set(CPACK_NSIS_MUI_UNIICON "${CMAKE_SOURCE_DIR}/zv/zv.ico")
    
    set(CPACK_NSIS_EXECUTABLES_DIRECTORY ".")
    set(CPACK_NSIS_MUI_FINISHPAGE_RUN "zv.exe")

    set(CPACK_NSIS_CREATE_ICONS_EXTRA
        "CreateShortCut '$SMPROGRAMS\\\\$STARTMENU_FOLDER\\\\zv.lnk' '$INSTDIR\\\\zv.exe'"
    )
    set(CPACK_NSIS_DELETE_ICONS_EXTRA
        "Delete '$SMPROGRAMS\\\\$START_MENU\\\\zv.lnk'"
    )

    include(CPack)
elseif (UNIX)
    install(TARGETS zvbin 
         DESTINATION bin
    )

    # set(CPACK_CMAKE_GENERATOR "Unix Makefiles")
    set(CPACK_GENERATOR "TGZ")

    set(CPACK_PACKAGE_NAME "zv")
    set(CPACK_PACKAGE_VENDOR "Nicolas Burrus")
    set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "zv is a lightweight image viewer, inspired by xv.")
    set(CPACK_PACKAGE_VERSION_MAJOR ${PROJECT_VERSION_MAJOR})
    set(CPACK_PACKAGE_VERSION_MINOR ${PROJECT_VERSION_MINOR})
    set(CPACK_PACKAGE_VERSION_PATCH ${PROJECT_VERSION_PATCH})
    if ("${PROJECT_VERSION_PATCH}" STREQUAL "")
        set(CPACK_PACKAGE_VERSION "${CPACK_PACKAGE_VERSION_MAJOR}.${CPACK_PACKAGE_VERSION_MINOR}")
    else()
        set(CPACK_PACKAGE_VERSION "${CPACK_PACKAGE_VERSION_MAJOR}.${CPACK_PACKAGE_VERSION_MINOR}.${CPACK_PACKAGE_VERSION_PATCH}")
    endif()
    set(CPACK_PACKAGE_INSTALL_DIRECTORY "zv")

    set(CPACK_PACKAGE_EXECUTABLES "zv")
    set(CPACK_PACKAGE_FILE_NAME "zv-${PROJECT_VERSION}-Linux-amd64")
    set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_SOURCE_DIR}/LICENSE")
    set(CPACK_SOURCE_GENERATOR "ZIP")
    set(CPACK_SOURCE_PACKAGE_FILE_NAME "zv-${PROJECT_VERSION}")
    set(CPACK_SOURCE_STRIP_FILES "")
    set(CPACK_SOURCE_IGNORE_FILES "build/")
    list(APPEND CPACK_SOURCE_IGNORE_FILES "/\.git/")
    list(APPEND CPACK_SOURCE_IGNORE_FILES "/\.gitattributes")
    list(APPEND CPACK_SOURCE_IGNORE_FILES "/\.gitignore")
    list(APPEND CPACK_SOURCE_IGNORE_FILES "/\.github/")
    list(APPEND CPACK_SOURCE_IGNORE_FILES "/\.ipynb_checkpoints/")    
    set(CPACK_STRIP_FILES "bin/zv")

    include(CPack)
endif()

# Make sure the final output name is zv. Can't reuse the same target name as the lib.
set_target_properties(zvbin PROPERTIES OUTPUT_NAME zv)

