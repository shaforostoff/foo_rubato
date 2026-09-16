#pragma once

// Precompiled header.
//
// On Windows, foobar2000+atl.h is the SDK's umbrella: it pulls in pfc, the SDK
// proper, ATL and WTL, and sets the Windows version macros, so nothing here
// should define _WIN32_WINNT or STRICT by hand - the build does that (see
// FOO_RUBATO_WIN32_WINNT in CMakeLists.txt).
//
// On macOS there is no ATL, no WTL and no libPPUI - they are Win32 window
// classes - so the umbrella is the SDK alone. The component's windows there
// are Cocoa, and live in mac/, which does not use this header: CMake puts the
// precompiled header in front of the C++ translation units only, and the
// Objective-C++ ones include what they need.

#ifdef _WIN32
#include <helpers/foobar2000+atl.h>
#include <helpers/atl-misc.h>
#include <helpers/input_helpers.h>
#include <libPPUI/listview_helper.h>
#else
#include <SDK/foobar2000.h>
#include <helpers/input_helpers.h>
#endif

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>
