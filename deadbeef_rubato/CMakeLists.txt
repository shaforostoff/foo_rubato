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
#
# Linux:
#   cmake -S deadbeef_rubato -B build/ddb -DCMAKE_BUILD_TYPE=Release
#   cmake --build build/ddb
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

configure_file(rubato_version.h.in "${CMAKE_CURRENT_BINARY_DIR}/rubato_version.h" @ONLY)

# --- the plugin ------------------------------------------------------------
# Compiled once as an object library, so the host test links exactly the code
# the player loads.
add_library(rubato_plugin_objects OBJECT
    rubato_plugin.cpp
    rubato_plugin.h
    rubato_format.h
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

# DeaDBeeF derives the entry point from the file name - ddb_rubato.<ext> must
# export ddb_rubato_load - and looks for .dll, .dylib or .so by platform.
add_library(ddb_rubato MODULE $<TARGET_OBJECTS:rubato_plugin_objects>)
target_link_libraries(ddb_rubato PRIVATE bpmcore Threads::Threads)
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
    target_link_options(ddb_rubato PRIVATE -static -static-libgcc -static-libstdc++)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    # Nothing is resolved from the host - the whole player API arrives as a
    # table of function pointers - so an undefined symbol is a mistake.
    target_link_options(ddb_rubato PRIVATE -Wl,--no-undefined)
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
    add_executable(ddb_rubato_hosttest tests/host_test.cpp)
    target_link_libraries(ddb_rubato_hosttest PRIVATE rubato_plugin_objects)
    add_test(NAME ddb_rubato_hosttest COMMAND ddb_rubato_hosttest)
endif()
