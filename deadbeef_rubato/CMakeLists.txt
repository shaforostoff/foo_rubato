# ---------------------------------------------------------------------------
# Rubato BPM Analyzer for DeaDBeeF
#
# A project of its own rather than a subdirectory of the top level, which is
# the foobar2000 component and needs the foobar2000 SDK to configure at all.
# This needs nothing but a compiler: the DeaDBeeF plugin header is in
# include/, and bpmcore and the transforms are built from ../.
#
# Windows (Visual Studio):
#   cmake -S deadbeef_rubato -B build/ddb-x64 -A x64
#   cmake --build build/ddb-x64 --config Release
#
# Windows (MSYS2 MinGW, the toolchain DeaDBeeF itself is built with):
#   cmake -S deadbeef_rubato -B build/ddb-mingw -G Ninja -DCMAKE_BUILD_TYPE=Release
#   cmake --build build/ddb-mingw
#
# macOS (universal, Apple Silicon and Intel):
#   cmake -S deadbeef_rubato -B build/ddb-mac -DCMAKE_BUILD_TYPE=Release
#   cmake --build build/ddb-mac
# or scripts/build_release_deadbeef_macos.sh for a release.
#
# Linux:
#   cmake -S deadbeef_rubato -B build/ddb -DCMAKE_BUILD_TYPE=Release
#   cmake --build build/ddb
#
# -DRUBATO_DDB_GTK=ON builds the results, progress and tapping windows for
# DeaDBeeF's GTK 3 interface, and needs GTK 3 development files (pkg-config
# gtk+-3.0). On by default on Linux and with MinGW, where DeaDBeeF's interface
# is GTK 3; off by default for MSVC, which cannot link the GTK DeaDBeeF ships,
# and on macOS, where the interface is Cocoa.
#
# -DRUBATO_DDB_COCOA=ON builds the same windows in Cocoa for DeaDBeeF for Mac,
# and needs nothing beyond the macOS SDK. On by default on macOS, and only
# there. One or the other, not both.
#
# With neither, the plugin has no windows: a scan writes as soon as it
# finishes and reports in the log.
#
# Then `ctest --test-dir <build dir> -C Release`. See README.md for where the
# built library goes on each platform.
# ---------------------------------------------------------------------------

cmake_minimum_required(VERSION 3.16)

if(POLICY CMP0091)
    cmake_policy(SET CMP0091 NEW)   # CMAKE_MSVC_RUNTIME_LIBRARY
endif()

# Universal by default: DeaDBeeF for Mac ships as one, and a plugin loads only
# into a process of its own architecture. Set before project(), which is when
# CMake settles it.
if(APPLE AND NOT CMAKE_OSX_ARCHITECTURES)
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "Architectures to build")
endif()
if(APPLE AND NOT CMAKE_OSX_DEPLOYMENT_TARGET)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE STRING "Minimum macOS version")
endif()

# The version has one home, project(VERSION) in the top-level CMakeLists.txt,
# and it is read from there rather than repeated.
get_filename_component(RUBATO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/.." ABSOLUTE)
file(READ "${RUBATO_ROOT}/CMakeLists.txt" _top_level)
if(NOT _top_level MATCHES "project\\(foo_rubato[^)]*VERSION[ \t\r\n]+([0-9]+)\\.([0-9]+)\\.([0-9]+)")
    message(FATAL_ERROR "Could not read the version out of ${RUBATO_ROOT}/CMakeLists.txt")
endif()
set(RUBATO_VERSION_MAJOR ${CMAKE_MATCH_1})
set(RUBATO_VERSION_MINOR ${CMAKE_MATCH_2})
set(RUBATO_VERSION "${CMAKE_MATCH_1}.${CMAKE_MATCH_2}.${CMAKE_MATCH_3}")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${RUBATO_ROOT}/CMakeLists.txt")

project(ddb_rubato
        VERSION ${RUBATO_VERSION}
        DESCRIPTION "Rubato BPM Analyzer for DeaDBeeF"
        LANGUAGES C CXX)   # kiss_fft and pffft are C

if(NOT CMAKE_CONFIGURATION_TYPES AND NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
# The static libraries end up inside a shared one.
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
# One exported symbol, ddb_rubato_load; nothing else of ours should be able to
# collide with another plugin's.
set(CMAKE_C_VISIBILITY_PRESET hidden)
set(CMAKE_CXX_VISIBILITY_PRESET hidden)
set(CMAKE_VISIBILITY_INLINES_HIDDEN ON)

# No Visual C++ redistributable needed on the machine it is installed on.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

if(MSVC)
    add_compile_options(/utf-8 /EHsc)
    # x64 is SSE2 by definition; on x86 it has to be asked for, and pffft
    # reads it to pick its SIMD path.
    if(CMAKE_SIZEOF_VOID_P EQUAL 4)
        add_compile_options(/arch:SSE2)
    endif()
elseif(MINGW AND CMAKE_SIZEOF_VOID_P EQUAL 4)
    add_compile_options(-msse2 -mfpmath=sse)
endif()

# Which transform, at what width - the same choice, and the same default, as
# the foobar2000 build. -DBPMCORE_FFT_BACKEND=kiss for the reference.
include("${RUBATO_ROOT}/cmake/fft_backend.cmake")

add_subdirectory("${RUBATO_ROOT}/kiss_fft" kiss_fft)
add_subdirectory("${RUBATO_ROOT}/pffft" pffft)
add_subdirectory("${RUBATO_ROOT}/bpmcore" bpmcore)

find_package(Threads REQUIRED)

if(APPLE OR MSVC)
    set(_rubato_gtk_default OFF)
else()
    set(_rubato_gtk_default ON)
endif()
option(RUBATO_DDB_GTK "Build the windows for DeaDBeeF's GTK 3 interface" ${_rubato_gtk_default})
option(RUBATO_DDB_COCOA "Build the windows for DeaDBeeF's Cocoa interface, on macOS" ${APPLE})
if(RUBATO_DDB_COCOA AND NOT APPLE)
    message(FATAL_ERROR "RUBATO_DDB_COCOA is for macOS; turn it off here.")
endif()
if(RUBATO_DDB_COCOA AND RUBATO_DDB_GTK)
    message(FATAL_ERROR "RUBATO_DDB_GTK and RUBATO_DDB_COCOA are both on; a build has one set of windows.")
endif()
if(RUBATO_DDB_COCOA)
    enable_language(OBJCXX)
    set(CMAKE_OBJCXX_STANDARD 17)
    set(CMAKE_OBJCXX_STANDARD_REQUIRED ON)
    set(CMAKE_OBJCXX_EXTENSIONS OFF)
    set(CMAKE_OBJCXX_VISIBILITY_PRESET hidden)
    message(STATUS "Windows: Cocoa")
elseif(RUBATO_DDB_GTK)
    find_package(PkgConfig)
    if(PKG_CONFIG_FOUND)
        pkg_check_modules(GTK3 IMPORTED_TARGET gtk+-3.0>=3.10)
    endif()
    if(NOT GTK3_FOUND)
        message(FATAL_ERROR
            "RUBATO_DDB_GTK is on but GTK 3.10 or later was not found through pkg-config.\n"
            "Install the GTK 3 development files (libgtk-3-dev; in MSYS2 "
            "mingw-w64-x86_64-gtk3), or configure with -DRUBATO_DDB_GTK=OFF "
            "for a plugin without windows.")
    endif()
    message(STATUS "Windows: GTK ${GTK3_VERSION}")
else()
    message(STATUS "Windows: none")
endif()
if(RUBATO_DDB_GTK OR RUBATO_DDB_COCOA)
    set(RUBATO_WINDOWS ON)
else()
    set(RUBATO_WINDOWS OFF)
endif()

configure_file(rubato_version.h.in "${CMAKE_CURRENT_BINARY_DIR}/rubato_version.h" @ONLY)

# --- the plugin ------------------------------------------------------------
# Compiled once as an object library, so the host test links exactly the code
# the player loads.
# The window implementation is not in it: that is chosen per build below, and
# the host test links a recording stand-in in its place.
add_library(rubato_plugin_objects OBJECT
    rubato_plugin.cpp
    rubato_plugin.h
    rubato_results.cpp
    rubato_results.h
    rubato_format.h
    rubato_ui.h
    rubato_version.h.in
    include/deadbeef/deadbeef.h)
target_include_directories(rubato_plugin_objects PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
    "${CMAKE_CURRENT_BINARY_DIR}")
if(MSVC)
    # deadbeef.h includes <dirent.h> and <unistd.h>, which MSVC does not have.
    target_include_directories(rubato_plugin_objects PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/compat/msvc")
    target_compile_definitions(rubato_plugin_objects PUBLIC _CRT_SECURE_NO_WARNINGS)
    target_compile_options(rubato_plugin_objects PRIVATE /W4)
else()
    target_compile_options(rubato_plugin_objects PRIVATE -Wall -Wextra)
endif()
target_link_libraries(rubato_plugin_objects PUBLIC bpmcore Threads::Threads)
# Whether the settings page offers the options that are about windows.
target_compile_definitions(rubato_plugin_objects PRIVATE
    RUBATO_HAVE_WINDOWS=$<BOOL:${RUBATO_WINDOWS}>)

if(RUBATO_DDB_GTK)
    add_library(rubato_ui OBJECT gtk/ui_gtk.cpp include/deadbeef/gtkui_api.h)
    target_link_libraries(rubato_ui PUBLIC PkgConfig::GTK3)
elseif(RUBATO_DDB_COCOA)
    add_library(rubato_ui OBJECT cocoa/ui_cocoa.mm)
    target_compile_options(rubato_ui PRIVATE -fobjc-arc -Wall -Wextra)
    # AppKit is DeaDBeeF's, already loaded; the plugin only links against it.
    target_link_libraries(rubato_ui PUBLIC "-framework Cocoa")
else()
    add_library(rubato_ui OBJECT ui_none.cpp)
endif()
target_include_directories(rubato_ui PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
    $<TARGET_PROPERTY:bpmcore,INTERFACE_INCLUDE_DIRECTORIES>)
if(MSVC)
    target_include_directories(rubato_ui PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/compat/msvc")
endif()

# DeaDBeeF derives the entry point from the file name - ddb_rubato.<ext> must
# export ddb_rubato_load - and looks for .dll, .dylib or .so by platform.
add_library(ddb_rubato MODULE $<TARGET_OBJECTS:rubato_plugin_objects> $<TARGET_OBJECTS:rubato_ui>)
target_link_libraries(ddb_rubato PRIVATE bpmcore Threads::Threads)
if(RUBATO_DDB_GTK)
    # Resolved against the GTK the player has already loaded.
    target_link_libraries(ddb_rubato PRIVATE PkgConfig::GTK3)
elseif(RUBATO_DDB_COCOA)
    target_link_libraries(ddb_rubato PRIVATE "-framework Cocoa")
endif()
set_target_properties(ddb_rubato PROPERTIES OUTPUT_NAME "ddb_rubato" PREFIX "")
if(APPLE)
    set_target_properties(ddb_rubato PROPERTIES SUFFIX ".dylib")
elseif(WIN32)
    set_target_properties(ddb_rubato PROPERTIES SUFFIX ".dll")
endif()

if(APPLE)
    # Apple Silicon will not map unsigned code, and the linker does not sign a
    # universal binary for us. DeaDBeeF does not run under the hardened
    # runtime, so an ad-hoc signature is all it takes; -DRUBATO_CODESIGN_IDENTITY
    # names a real one.
    set(RUBATO_CODESIGN_IDENTITY "-" CACHE STRING "codesign identity; - for ad-hoc")
    add_custom_command(TARGET ddb_rubato POST_BUILD
        COMMAND codesign --force --sign "${RUBATO_CODESIGN_IDENTITY}" "$<TARGET_FILE:ddb_rubato>"
        VERBATIM)
elseif(MINGW)
    # Everything the plugin needs inside the one file: libstdc++, libgcc and
    # winpthreads, which a DeaDBeeF install may carry in another version or
    # not at all.
    # GTK stays a DLL: it is DeaDBeeF's own copy that the windows must draw
    # with, so only the C++ runtime is linked in when there is GTK.
    if(RUBATO_DDB_GTK)
        target_link_options(ddb_rubato PRIVATE -static-libgcc -static-libstdc++)
    else()
        target_link_options(ddb_rubato PRIVATE -static -static-libgcc -static-libstdc++)
    endif()
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    # Nothing is resolved from the host - the whole player API arrives as a
    # table of function pointers - so an undefined symbol is a mistake.
    target_link_options(ddb_rubato PRIVATE -Wl,--no-undefined)
endif()

# Export ddb_rubato_load and nothing else. Hidden visibility leaves out the
# std:: templates the plugin instantiates, which the standard headers mark
# visible; the linker is what can be told the whole list. Windows exports only
# what is marked dllexport already.
if(APPLE)
    target_link_options(ddb_rubato PRIVATE "LINKER:-exported_symbol,_ddb_rubato_load")
elseif(NOT WIN32)
    target_link_options(ddb_rubato PRIVATE
        "LINKER:--version-script=${CMAKE_CURRENT_SOURCE_DIR}/ddb_rubato.map")
    set_property(TARGET ddb_rubato APPEND PROPERTY LINK_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/ddb_rubato.map")
endif()

# --- tests -----------------------------------------------------------------
option(RUBATO_DDB_BUILD_TESTS "Build the DeaDBeeF plugin's tests" ON)
if(RUBATO_DDB_BUILD_TESTS)
    enable_testing()

    # The strings that reach people's files.
    add_executable(rubato_format_test tests/format_test.cpp)
    target_link_libraries(rubato_format_test PRIVATE bpmcore)
    target_include_directories(rubato_format_test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    add_test(NAME rubato_format_test COMMAND rubato_format_test)

    # The plugin driven through a stand-in for the player: actions, decoding,
    # analysis and every field written, without DeaDBeeF installed.
    add_executable(ddb_rubato_hosttest tests/host_test.cpp tests/fake_host.cpp tests/fake_host.h
        tests/ui_recorder.cpp)
    target_link_libraries(ddb_rubato_hosttest PRIVATE rubato_plugin_objects)
    add_test(NAME ddb_rubato_hosttest COMMAND ddb_rubato_hosttest)

    # The GTK windows on the same stand-in, with a synthetic scan behind
    # them. Not a test - it puts windows up - but the way to look at them
    # without DeaDBeeF: `rubato_gtk_preview` to use them, or with an output
    # directory to draw each one to a PNG and exit.
    if(RUBATO_DDB_GTK)
        add_executable(rubato_gtk_preview tests/gtk_preview.cpp tests/fake_host.cpp
            tests/fake_host.h tests/preview_tracks.h $<TARGET_OBJECTS:rubato_ui>)
        target_link_libraries(rubato_gtk_preview PRIVATE rubato_plugin_objects PkgConfig::GTK3)
    endif()
    # The same for the Cocoa windows.
    if(RUBATO_DDB_COCOA)
        add_executable(rubato_cocoa_preview tests/cocoa_preview.mm tests/fake_host.cpp
            tests/fake_host.h tests/preview_tracks.h $<TARGET_OBJECTS:rubato_ui>)
        target_compile_options(rubato_cocoa_preview PRIVATE -fobjc-arc)
        target_link_libraries(rubato_cocoa_preview PRIVATE rubato_plugin_objects "-framework Cocoa")
    endif()
endif()
