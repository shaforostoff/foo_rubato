# ---------------------------------------------------------------------------
# Rubato BPM Analyzer for foobar2000
#
# Windows:
#   cmake -S . -B build/x64 -A x64
#   cmake --build build/x64 --config Release
#
# macOS:
#   cmake -S . -B build/mac -DCMAKE_BUILD_TYPE=Release
#   cmake --build build/mac
#
# Or just run scripts\build_release.ps1 or scripts/build_release_macos.sh,
# which do every architecture the platform ships and package the result. The
# SDK - and on Windows WTL - are fetched automatically the first time you
# configure; see README.md.
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

if(NOT WIN32 AND NOT APPLE)
    message(FATAL_ERROR
        "foo_rubato targets the Windows and macOS builds of foobar2000.")
endif()

# The macOS half of the component is Cocoa, so two more languages - and after
# project(), because that is what settles which platform this is. Objective-C
# is here for the SDK's own .m files; ours are all Objective-C++.
if(APPLE)
    enable_language(OBJC)
    enable_language(OBJCXX)
endif()

# --- options ---------------------------------------------------------------
set(FB2K_SDK_DIR "${CMAKE_CURRENT_SOURCE_DIR}/external/foobar2000_sdk"
    CACHE PATH "Location of the unpacked foobar2000 SDK")
set(FB2K_WTL_DIR "${CMAKE_CURRENT_SOURCE_DIR}/external/wtl"
    CACHE PATH "Location of the unpacked WTL (Windows only)")
option(FB2K_SDK_AUTO_DOWNLOAD "Fetch the SDK and WTL if they are not present" ON)
option(FOO_RUBATO_STATIC_CRT "Link the C runtime statically (no VC redist needed)" ON)
option(FOO_RUBATO_LTO        "Whole program optimisation in Release builds" ON)
set(FOO_RUBATO_WIN32_WINNT "0x0601" CACHE STRING "Minimum Windows version (0x0601 = Windows 7)")

# macOS 11 Big Sur, which is what foobar2000 for Mac itself requires and what
# the SDK's own Xcode projects target. Set before any target is declared, so
# every object in the build agrees.
if(APPLE)
    if(NOT CMAKE_OSX_DEPLOYMENT_TARGET)
        set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version" FORCE)
    endif()
endif()

# --- target architecture ---------------------------------------------------
# On Windows one configure is one architecture, chosen with -A. On macOS one
# configure can be both at once - that is what a universal binary is - so the
# answer here is a list rather than a word.
if(APPLE)
    if(CMAKE_OSX_ARCHITECTURES)
        set(FOO_RUBATO_TARGET_ARCH "${CMAKE_OSX_ARCHITECTURES}")
    else()
        set(FOO_RUBATO_TARGET_ARCH "${CMAKE_SYSTEM_PROCESSOR}")
    endif()
elseif(CMAKE_SIZEOF_VOID_P EQUAL 8)
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
# WTL is Win32 window classes and the macOS build has no use for it, so it is
# only looked for - and only fetched - where it is needed.
set(_fb2k_deps_present TRUE)
if(NOT EXISTS "${FB2K_SDK_DIR}/foobar2000/SDK/foobar2000.h")
    set(_fb2k_deps_present FALSE)
endif()
if(WIN32 AND NOT EXISTS "${FB2K_WTL_DIR}/Include/atlapp.h")
    set(_fb2k_deps_present FALSE)
endif()

if(NOT _fb2k_deps_present)
    if(NOT FB2K_SDK_AUTO_DOWNLOAD)
        message(FATAL_ERROR
            "The foobar2000 SDK was not found under ${CMAKE_CURRENT_SOURCE_DIR}/external.\n"
            "Run scripts/get_sdk.ps1 (Windows) or scripts/get_sdk.sh (macOS), "
            "or configure with -DFB2K_SDK_AUTO_DOWNLOAD=ON.")
    endif()
    if(WIN32)
        # PowerShell rather than file(DOWNLOAD): see the note in scripts/get_sdk.ps1.
        find_program(FOO_RUBATO_POWERSHELL NAMES powershell.exe pwsh.exe REQUIRED)
        execute_process(
            COMMAND "${FOO_RUBATO_POWERSHELL}" -NoProfile -ExecutionPolicy Bypass
                    -File "${CMAKE_CURRENT_SOURCE_DIR}/scripts/get_sdk.ps1"
            RESULT_VARIABLE _sdk_result)
    else()
        find_program(FOO_RUBATO_BASH NAMES bash REQUIRED)
        execute_process(
            COMMAND "${FOO_RUBATO_BASH}" "${CMAKE_CURRENT_SOURCE_DIR}/scripts/get_sdk.sh"
                    -d "${CMAKE_CURRENT_SOURCE_DIR}/external"
            RESULT_VARIABLE _sdk_result)
    endif()
    if(NOT _sdk_result EQUAL 0)
        message(FATAL_ERROR "Could not obtain the build prerequisites (exit ${_sdk_result}).")
    endif()
endif()

# --- global build settings -------------------------------------------------
# C++17 everywhere our own code is concerned. The SDK's Xcode projects build it
# at gnu++20; nothing in its headers needs C++20 from a consumer, and holding
# both platforms to one standard is worth more than matching that.
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_OBJCXX_STANDARD 17)
set(CMAKE_OBJCXX_STANDARD_REQUIRED ON)
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

if(APPLE)
    # ARC for every Objective-C object in the build, including the SDK's, which
    # is what its own Xcode projects use. Getting this wrong is not a warning:
    # a file compiled without ARC that hands an object to one compiled with it
    # leaks or over-releases, and neither shows up until run time.
    add_compile_options($<$<COMPILE_LANGUAGE:OBJC,OBJCXX>:-fobjc-arc>)
    # A component's symbols are its own. The host resolves nothing for us -
    # every SDK symbol is statically linked into the bundle - and two
    # components that both export, say, `_init` would otherwise be able to see
    # each other's.
    add_compile_options(-fvisibility=hidden -fvisibility-inlines-hidden)
    # Nothing in this build wants to be told that a signed/unsigned comparison
    # in the SDK's headers is a signed/unsigned comparison; our own targets ask
    # for the warnings they want.
    add_compile_options(-Wall)
    # Debug info in Release too, for the same reason the MSVC branch asks for a
    # PDB there: a crash report is only useful with symbols, and -g changes
    # nothing about the code generated. scripts/build_release_macos.sh pulls it
    # out into a .dSYM and strips the shipped binary, so this costs the
    # component nothing on disk either.
    add_compile_options($<$<NOT:$<CONFIG:Debug>>:-g>)
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
    # dialog_test draws Win32 dialog templates out of the built DLL. There are
    # no dialog templates in the macOS component - its windows are built in
    # code, where a clipped label is a layout constraint rather than a resource
    # - so there is nothing there for it to read.
    if(WIN32)
        add_subdirectory(dialog_test)
    endif()
endif()
