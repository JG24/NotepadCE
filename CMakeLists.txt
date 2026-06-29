cmake_minimum_required(VERSION 3.16)
project(NotepadCE VERSION 0.9.2 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Default to MinSizeRel for single-config generators when not specified
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE MinSizeRel CACHE STRING "Build type" FORCE)
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS Debug Release MinSizeRel RelWithDebInfo)
endif()

# Enable LTO/IPO for non-Debug builds.
# GCC 16.x has ICEs in LTO pass with C++17 lambdas + switch statements,
# so we disable IPO for that version range. Revisit after 16.2 is out.
include(CheckIPOSupported)
check_ipo_supported(RESULT IPO_SUPPORTED OUTPUT IPO_ERROR)
set(_ipo_blocklisted FALSE)
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND CMAKE_CXX_COMPILER_VERSION VERSION_GREATER_EQUAL 16
                                       AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 17)
    set(_ipo_blocklisted TRUE)
endif()
if(IPO_SUPPORTED AND NOT _ipo_blocklisted AND NOT CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION TRUE)
endif()

# Use static runtime for MSVC
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

option(LEGACY_NOTEPAD_WERROR "Treat warnings as errors" OFF)

if(MSVC)
    add_compile_options(/W4 /WX /permissive- /utf-8)
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} /SUBSYSTEM:WINDOWS /ENTRY:wWinMainCRTStartup")
else()
    add_compile_options(-Wall -Wextra -municode -Wno-error=missing-field-initializers -finput-charset=UTF-8 -fexec-charset=UTF-8)
    # Place each function/data item in its own section so the linker can
    # drop everything unreferenced (--gc-sections below). A safe, no-ICE
    # substitute for LTO, which is blocklisted on GCC 16.x above.
    add_compile_options(-ffunction-sections -fdata-sections)
    if(LEGACY_NOTEPAD_WERROR)
        add_compile_options(-Werror)
    endif()
    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -static -static-libgcc -static-libstdc++ -mwindows")
    else()
        set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -mwindows")
    endif()
endif()

add_executable(NotepadCE WIN32
    src/main.cpp)
target_sources(NotepadCE PRIVATE
    src/core/globals.cpp
    src/lang/lang.cpp
    src/modules/theme.cpp
    src/modules/editor.cpp
    src/modules/file.cpp
    src/modules/ui.cpp
    src/modules/dialog.cpp
    src/modules/commands.cpp
    src/modules/menu.cpp
    src/modules/settings.cpp
    src/modules/spellchecker.cpp
    src/modules/tools.cpp
    src/modules/gutter.cpp
    src/modules/quickicons.cpp
    src/notepad.rc
)

target_include_directories(NotepadCE PRIVATE ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/src/modules)
target_compile_definitions(NotepadCE PRIVATE
    UNICODE _UNICODE
    WINVER=0x0602
    _WIN32_WINNT=0x0602
    NTDDI_VERSION=0x06020000
)

target_link_libraries(NotepadCE PRIVATE
    comctl32
    shlwapi
    comdlg32
    shell32
    user32
    gdi32
    msimg32
    kernel32
    dwmapi
    uxtheme
    ole32
    bcrypt
    crypt32
)

if(NOT MSVC)
    target_link_options(NotepadCE PRIVATE -mwindows -municode)
    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        target_link_options(NotepadCE PRIVATE -static)
        target_link_libraries(NotepadCE PRIVATE -static-libgcc -static-libstdc++)
    endif()
    # Strip during link instead of post-build: avoids a second file-write step
    # that AV/Defender real-time scanning frequently blocks on Windows.
    # --gc-sections discards sections no longer referenced after linking.
    # Keep symbols in Debug builds so crashes can be backtraced (gdb/addr2line).
    target_link_options(NotepadCE PRIVATE -Wl,--gc-sections)
    if(NOT CMAKE_BUILD_TYPE STREQUAL "Debug")
        target_link_options(NotepadCE PRIVATE -s)
    endif()
endif()

# Build timestamp — generates build_info.h (#define BUILD_NUMBER) used by
# the About dialog. Generated once at configure time so the header exists,
# then refreshed by a pre-build custom target so the value tracks each
# actual compile. The script no-ops when the minute hasn't changed.
execute_process(COMMAND ${CMAKE_COMMAND}
                -DOUTPUT=${CMAKE_BINARY_DIR}/build_info.h
                -P ${CMAKE_SOURCE_DIR}/gen_build_info.cmake)
add_custom_target(generate_build_info
    COMMAND ${CMAKE_COMMAND}
            -DOUTPUT=${CMAKE_BINARY_DIR}/build_info.h
            -P ${CMAKE_SOURCE_DIR}/gen_build_info.cmake
    COMMENT "Refreshing build timestamp")
add_dependencies(NotepadCE generate_build_info)
target_include_directories(NotepadCE PRIVATE ${CMAKE_BINARY_DIR})

# Installation
install(TARGETS NotepadCE DESTINATION .)
install(FILES LICENSE README.md DESTINATION .)

# Installer / packaging (CPack with NSIS)
set(CPACK_GENERATOR "NSIS")
set(CPACK_PACKAGE_NAME "NotepadCE")
set(CPACK_PACKAGE_VENDOR "Legacy Notepad")
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
if(CMAKE_GENERATOR_PLATFORM)
    set(CPACK_PACKAGE_FILE_NAME "NotepadCE-${PROJECT_VERSION}-${CMAKE_GENERATOR_PLATFORM}")
else()
    set(CPACK_PACKAGE_FILE_NAME "NotepadCE-${PROJECT_VERSION}")
endif()
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_SOURCE_DIR}/LICENSE")
set(CPACK_NSIS_DISPLAY_NAME "Legacy Notepad")
set(CPACK_NSIS_PACKAGE_NAME "Legacy Notepad")
set(CPACK_NSIS_URL_INFO_ABOUT "https://github.com/ForLoopCodes/NotepadCE")
set(CPACK_NSIS_MODIFY_PATH ON)
set(CPACK_NSIS_ENABLE_UNINSTALL_BEFORE_INSTALL ON)
set(CPACK_NSIS_MUI_ICON "${CMAKE_SOURCE_DIR}/src/icon.ico")
set(CPACK_NSIS_MUI_UNIICON "${CMAKE_SOURCE_DIR}/src/icon.ico")

include(CPack)
