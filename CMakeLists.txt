# ---------------------------------------------------------------------------
# Rubato BPM Analyzer for foobar2000
#
#   cmake -S . -B build/x64 -A x64
#   cmake --build build/x64 --config Release
#
# Or just run scripts\build_release.ps1, which does both architectures and
# packages the result. The SDK and WTL are fetched automatically the first
# time you configure; see README.md.
# ---------------------------------------------------------------------------

cmake_minimum_required(VERSION 3.21)

# Pick the MSVC runtime through CMAKE_MSVC_RUNTIME_LIBRARY rather than by
# rewriting CMAKE_CXX_FLAGS.
if(POLICY CMP0091)
    cmake_policy(SET CMP0091 NEW)
endif()

project(foo_rubato
        VERSION 0.1.0
        DESCRIPTION "Rubato BPM Analyzer for foobar2000"
        LANGUAGES C CXX)   # kiss_fft and pffft are C

if(NOT WIN32)
    message(FATAL_ERROR "foo_rubato targets the Windows build of foobar2000.")
endif()

# --- options ---------------------------------------------------------------
set(FB2K_SDK_DIR "${CMAKE_CURRENT_SOURCE_DIR}/external/foobar2000_sdk"
    CACHE PATH "Location of the unpacked foobar2000 SDK")
set(FB2K_WTL_DIR "${CMAKE_CURRENT_SOURCE_DIR}/external/wtl"
    CACHE PATH "Location of the unpacked WTL")
option(FB2K_SDK_AUTO_DOWNLOAD "Fetch the SDK and WTL if they are not present" ON)
option(FOO_RUBATO_STATIC_CRT "Link the C runtime statically (no VC redist needed)" ON)
option(FOO_RUBATO_LTO        "Whole program optimisation in Release builds" ON)
set(FOO_RUBATO_WIN32_WINNT "0x0601" CACHE STRING "Minimum Windows version (0x0601 = Windows 7)")

# --- target architecture ---------------------------------------------------
if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "[Aa][Rr][Mm]64")
        set(FOO_RUBATO_TARGET_ARCH "ARM64EC")
    else()
        set(FOO_RUBATO_TARGET_ARCH "x64")
    endif()
else()
    set(FOO_RUBATO_TARGET_ARCH "x86")
endif()
message(STATUS "Target architecture: ${FOO_RUBATO_TARGET_ARCH}")

# --- dependencies ----------------------------------------------------------
if(NOT EXISTS "${FB2K_SDK_DIR}/foobar2000/SDK/foobar2000.h" OR
   NOT EXISTS "${FB2K_WTL_DIR}/Include/atlapp.h")
    if(NOT FB2K_SDK_AUTO_DOWNLOAD)
        message(FATAL_ERROR
            "The foobar2000 SDK or WTL was not found under ${CMAKE_CURRENT_SOURCE_DIR}/external.\n"
            "Run scripts\\get_sdk.ps1, or configure with -DFB2K_SDK_AUTO_DOWNLOAD=ON.")
    endif()
    # PowerShell rather than file(DOWNLOAD): see the note in scripts/get_sdk.ps1.
    find_program(FOO_RUBATO_POWERSHELL NAMES powershell.exe pwsh.exe REQUIRED)
    execute_process(
        COMMAND "${FOO_RUBATO_POWERSHELL}" -NoProfile -ExecutionPolicy Bypass
                -File "${CMAKE_CURRENT_SOURCE_DIR}/scripts/get_sdk.ps1"
        RESULT_VARIABLE _sdk_result)
    if(NOT _sdk_result EQUAL 0)
        message(FATAL_ERROR "Could not obtain the build prerequisites (exit ${_sdk_result}).")
    endif()
endif()

# --- global build settings -------------------------------------------------
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

if(FOO_RUBATO_STATIC_CRT)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
else()
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
endif()

if(NOT CMAKE_CONFIGURATION_TYPES AND NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

if(MSVC)
    add_compile_options($<$<CONFIG:Release,RelWithDebInfo,MinSizeRel>:/O2>
                        $<$<CONFIG:Release,RelWithDebInfo,MinSizeRel>:/Oi>)
    add_compile_options(/EHsc /Zi /utf-8 /MP)
    # Keep foobar2000 SDK header warnings out of our build log.
    if(MSVC_VERSION GREATER_EQUAL 1929)
        add_compile_options(/external:W0)
    endif()
    # Produce a PDB for Release too: foobar2000's crash reporter is only useful
    # with symbols, and /DEBUG does not slow the generated code down.
    add_link_options($<$<NOT:$<CONFIG:Debug>>:/DEBUG>
                     $<$<NOT:$<CONFIG:Debug>>:/OPT:REF>
                     $<$<NOT:$<CONFIG:Debug>>:/OPT:ICF>)
    # x64 is SSE2 by definition; on x86 it has to be asked for, and it also
    # moves double maths off the x87 stack, which keeps results consistent
    # between the two builds.
    if(FOO_RUBATO_TARGET_ARCH STREQUAL "x86")
        add_compile_options(/arch:SSE2)
    endif()
endif()

if(FOO_RUBATO_LTO)
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELWITHDEBINFO ON)
endif()

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/fb2k_sdk.cmake")

# Settled before the targets, so kiss_fft, pffft and bpmcore cannot disagree
# about the width. bpmcore includes this too, for a standalone build.
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/fft_backend.cmake")

# Both transforms are always built, whichever one bpmcore links. kiss is two
# files and costs nothing to carry, and it is what pffft is checked against on
# each architecture - see fft_backend_test. That is worth more than a fallback
# nobody would ship.
add_subdirectory(kiss_fft)
add_subdirectory(pffft)
add_subdirectory(bpmcore)
add_subdirectory(foo_rubato)

option(FOO_RUBATO_BUILD_TESTS "Build the verification harnesses" ON)
if(FOO_RUBATO_BUILD_TESTS)
    enable_testing()
    add_subdirectory(kiss_fft_test)
    add_subdirectory(fft_backend_test)
    add_subdirectory(bpmcore_test)
    add_subdirectory(dialog_test)
endif()
