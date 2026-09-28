#ifndef RUBATO_UI_H
#define RUBATO_UI_H

// The plugin's windows, as the rest of it sees them.
//
// One implementation per toolkit, chosen when the plugin is built:
//
//   ui_none.cpp       no windows, in a build with neither RUBATO_DDB_GTK nor
//                     RUBATO_DDB_COCOA. The plugin writes as soon as a scan
//                     finishes and reports in the log.
//   gtk/ui_gtk.cpp    GTK 3, for DeaDBeeF's GTK 3 interface on Linux and
//                     Windows.
//   cocoa/ui_cocoa.mm Cocoa, for DeaDBeeF for Mac.
//
// Each implements the same functions against rubato_results.h and nothing
// else. What the windows show is decided there, not here.
//
// Every function may be called from any thread - a scan finishes on a worker
// thread - and an implementation hands the work to its window system's own
// thread. None of them blocks.

#include <memory>

#include "rubato_results.h"

namespace rubato
{
namespace ui
{

//! Called from the plugin's connect(), once every plugin has started: the
//! point at which the implementation can see whether the interface it draws
//! into is the one DeaDBeeF is running.
void connect(DB_functions_t * api);

//! Called from the plugin's stop(). Closes whatever is open and gives back
//! every track the windows were holding.
void shutdown();

//! Whether there is anywhere to put a window. False in a build without one,
//! and in one whose toolkit is not the interface DeaDBeeF is running - the
//! GTK 3 windows under the GTK 2 interface, say - in which case the plugin
//! behaves as it does without windows at all.
bool available();

//! A progress bar with a Cancel button, for a scan that has just started.
//! Shown only if the scan is still going after a moment, and closed by
//! itself when it finishes.
void show_progress(std::shared_ptr<scan_progress> progress);

//! The results window: one row per track, the double and halve buttons, and
//! the button that writes them.
void show_results(std::shared_ptr<results> r);

//! The window that measures the tempo of what is playing from the user
//! tapping along.
void show_tap();

}   // namespace ui
}   // namespace rubato

#endif // RUBATO_UI_H
