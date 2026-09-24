# The component itself.
#
# Windows: a DLL. foobar2000 loads components by filename, and foo_rubato.cpp
# asserts its own with VALIDATE_COMPONENT_FILENAME, so the DLL is named
# foo_rubato.dll on every architecture.
#
# macOS: a loadable bundle named foo_rubato.component. There is no filename
# assertion there - VALIDATE_COMPONENT_FILENAME compiles to nothing off Windows
# - but the name matters all the same, because the two platforms' payloads
# share one .fb2k-component archive and are told apart by where they sit in it:
#
#     foo_rubato.dll            32 bit Windows
#     x64/foo_rubato.dll        64 bit Windows
#     mac/foo_rubato.component  macOS
#
# scripts\build_release.ps1 and scripts/build_release_macos.sh assemble that.

# The SDK release is pinned in scripts\get_sdk.ps1, alongside the URL and the
# checksum - scripts/get_sdk.sh reads its pin out of that same file - so that is
# where the about box gets it from rather than stating a date of its own. WTL is
# pinned in the same file as '10.01', which the date pattern here will not match.
set(_get_sdk "${CMAKE_SOURCE_DIR}/scripts/get_sdk.ps1")
file(READ "${_get_sdk}" _get_sdk_text)
if(_get_sdk_text MATCHES "Version[ \t]*=[ \t]*'([0-9][0-9][0-9][0-9]-[0-9][0-9]-[0-9][0-9])'")
    set(FOO_RUBATO_SDK_VERSION "${CMAKE_MATCH_1}")
else()
    message(WARNING "Could not read the SDK release out of ${_get_sdk}.")
    set(FOO_RUBATO_SDK_VERSION "unknown")
endif()
# configure_file tracks version.h.in by itself, but not a file it merely read.
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_get_sdk}")

# The version reaches the sources only through this generated header, so
# project(VERSION) at the top level stays its single home. @ONLY because the
# template also holds ${}-free C preprocessor text.
configure_file(version.h.in "${CMAKE_CURRENT_BINARY_DIR}/version.h" @ONLY)

# --- sources ---------------------------------------------------------------
# Everything above the window system, which is both platforms' code.
set(FOO_RUBATO_SOURCES
    bpm_analysis.cpp
    bpm_auto_analysis_thread.cpp
    bpm_contextmenu_item.cpp
    bpm_tmpo_sync.cpp
    file_info_filter_bpm.cpp
    file_info_filter_scale_bpm.cpp
    foo_rubato.cpp
    format_bpm.cpp
    preferences.cpp

    bpm_analysis.h
    bpm_auto_analysis_thread.h
    bpm_contextmenu_item.h
    bpm_result_format.h
    bpm_tag_fields.h
    bpm_tmpo_sync.h
    bpm_track_result.h
    bpm_ui.h
    fft_license.h
    file_info_filter_bpm.h
    file_info_filter_scale_bpm.h
    format_bpm.h
    globals.h
    guid.h
    mp4_tmpo.h
    preferences.h
    resource.h
    stdafx.h
    version.h.in

    "${CMAKE_CURRENT_BINARY_DIR}/version.h")

# ... and the window system, which is not. Each platform supplies the two
# windows and the preferences page that bpm_ui.h and preferences_page declare;
# nothing above this line knows which.
if(WIN32)
    list(APPEND FOO_RUBATO_SOURCES
        bpm_manual_dialog.cpp
        bpm_preferences_page.cpp
        bpm_result_dialog.cpp
        foo_rubato.rc

        bpm_manual_dialog.h
        bpm_preferences_page.h
        bpm_result_dialog.h)
else()
    list(APPEND FOO_RUBATO_SOURCES
        mac/bpm_manual_window_mac.mm
        mac/bpm_preferences_page_mac.mm
        mac/bpm_result_window_mac.mm

        mac/foobar2000-mac-class-suffix.h
        mac/mac_strings.h

        # foobar2000_get_interface lives here; see cmake/fb2k_sdk.cmake for why
        # it is compiled into the component rather than linked out of the SDK.
        "${FB2K_COMPONENT_CLIENT_SOURCE}")
endif()

add_library(foo_rubato MODULE ${FOO_RUBATO_SOURCES})

if(WIN32)
    set_target_properties(foo_rubato PROPERTIES
        OUTPUT_NAME "foo_rubato"
        PREFIX ""
        SUFFIX ".dll")
else()
    # BUNDLE with a MODULE library is what makes CMake lay out
    # foo_rubato.component/Contents/MacOS/foo_rubato and put an Info.plist
    # beside it, which is the shape foobar2000 opens.
    set_target_properties(foo_rubato PROPERTIES
        OUTPUT_NAME "foo_rubato"
        BUNDLE TRUE
        BUNDLE_EXTENSION "component"
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/mac/Info.plist.in"
        MACOSX_BUNDLE_BUNDLE_NAME "Rubato BPM Analyzer"
        MACOSX_BUNDLE_GUI_IDENTIFIER "org.foobar2000.component.foo-rubato"
        MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
        MACOSX_BUNDLE_COPYRIGHT "(c) 2009-2014 Michael Balzer, 2014 Holger Stenger, 2026 Nick Shaforostov")
endif()

# kiss_fft is bpmcore's business now, and reaches the link through it: nothing
# here has included a kiss_fft header since the legacy engine's wrapper went.
# What does still cross is which backend was chosen, as a PUBLIC definition, and
# fft_license.h turns that into the one licence the about box prints.
target_link_libraries(foo_rubato PRIVATE fb2k::sdk bpmcore)

# ... which is where the generated version.h lands.
target_include_directories(foo_rubato PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")

if(WIN32)
    # windows.h defines min and max as macros, which breaks every std::min/std::max
    # in the analysis code. Neither the SDK nor WTL relies on the macros, so they
    # are simply turned off for this target.
    target_compile_definitions(foo_rubato PRIVATE NOMINMAX)
endif()

if(APPLE AND FOO_RUBATO_LTO)
    # With LTO the linker compiles the whole program into temporary object
    # files and throws them away, and every one of them has the same name and
    # the same timestamp. dsymutil finds debug info by following the link map
    # back to those objects, so it keeps the first and skips the rest - and
    # says so as a warning rather than an error, leaving a .dSYM that looks
    # like a .dSYM and holds almost nothing. Giving the linker a directory to
    # keep them in gives each one a name of its own.
    set(_foo_rubato_lto_objects "${CMAKE_CURRENT_BINARY_DIR}/lto")
    file(MAKE_DIRECTORY "${_foo_rubato_lto_objects}")
    target_link_options(foo_rubato PRIVATE
        "$<$<NOT:$<CONFIG:Debug>>:LINKER:-object_path_lto,${_foo_rubato_lto_objects}>")
endif()

# stdafx.h is the precompiled header: it pulls in the SDK, ATL and WTL, which
# dwarf this component's own sources. C++ only - the Objective-C++ half of the
# macOS build includes Cocoa as well and is three files, which is not worth a
# second precompiled header.
target_precompile_headers(foo_rubato PRIVATE
    "$<$<COMPILE_LANGUAGE:CXX>:${CMAKE_CURRENT_SOURCE_DIR}/stdafx.h>")

if(MSVC)
    # /wd4100: ATL's message crackers and the SDK's callback interfaces fix the
    # parameter list of every handler, so unused parameters are the norm here
    # and flagging them would bury the warnings worth reading.
    target_compile_options(foo_rubato PRIVATE /W4 /wd4100)
else()
    # -Wno-unused-parameter for the same reason /wd4100 is set above: the SDK's
    # interfaces fix the parameter list of every override.
    target_compile_options(foo_rubato PRIVATE -Wall -Wextra -Wno-unused-parameter)
endif()

set_target_properties(foo_rubato PROPERTIES FOLDER "foo_rubato")
