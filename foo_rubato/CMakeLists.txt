# The component itself.
#
# foobar2000 loads components by filename, and foo_rubato.cpp asserts its own
# with VALIDATE_COMPONENT_FILENAME, so the DLL is named foo_rubato.dll on every
# architecture. The .fb2k-component archive that scripts\build_release.ps1
# assembles puts the x64 build in an x64\ subfolder, which is how foobar2000
# tells the two apart.

# The SDK release is pinned in scripts\get_sdk.ps1, alongside the URL and the
# checksum, so that is where the about box gets it from rather than stating a
# date of its own. WTL is pinned in the same file as '10.01', which the date
# pattern here will not match.
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

add_library(foo_rubato MODULE
    bpm_analysis.cpp
    bpm_auto_analysis_thread.cpp
    bpm_contextmenu_item.cpp
    bpm_manual_dialog.cpp
    bpm_preferences_page.cpp
    bpm_result_dialog.cpp
    file_info_filter_bpm.cpp
    file_info_filter_scale_bpm.cpp
    foo_rubato.cpp
    format_bpm.cpp
    preferences.cpp
    foo_rubato.rc

    bpm_analysis.h
    bpm_auto_analysis_thread.h
    bpm_contextmenu_item.h
    bpm_manual_dialog.h
    bpm_preferences_page.h
    bpm_result_dialog.h
    bpm_result_format.h
    bpm_ui.h
    fft_license.h
    file_info_filter_bpm.h
    file_info_filter_scale_bpm.h
    format_bpm.h
    globals.h
    guid.h
    preferences.h
    resource.h
    stdafx.h
    version.h.in

    "${CMAKE_CURRENT_BINARY_DIR}/version.h")

set_target_properties(foo_rubato PROPERTIES
    OUTPUT_NAME "foo_rubato"
    PREFIX ""
    SUFFIX ".dll")

# kiss_fft is bpmcore's business now, and reaches the link through it: nothing
# here has included a kiss_fft header since the legacy engine's wrapper went.
# What does still cross is which backend was chosen, as a PUBLIC definition, and
# fft_license.h turns that into the one licence the about box prints.
target_link_libraries(foo_rubato PRIVATE fb2k::sdk bpmcore)

# ... which is where the generated version.h lands.
target_include_directories(foo_rubato PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")

# windows.h defines min and max as macros, which breaks every std::min/std::max
# in the analysis code. Neither the SDK nor WTL relies on the macros, so they
# are simply turned off for this target.
target_compile_definitions(foo_rubato PRIVATE NOMINMAX)

# stdafx.h is the precompiled header: it pulls in the SDK, ATL and WTL, which
# dwarf this component's own sources.
target_precompile_headers(foo_rubato PRIVATE stdafx.h)

if(MSVC)
    # /wd4100: ATL's message crackers and the SDK's callback interfaces fix the
    # parameter list of every handler, so unused parameters are the norm here
    # and flagging them would bury the warnings worth reading.
    target_compile_options(foo_rubato PRIVATE /W4 /wd4100)
endif()

set_target_properties(foo_rubato PROPERTIES FOLDER "foo_rubato")
