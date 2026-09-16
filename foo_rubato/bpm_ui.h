#ifndef __BPM_UI_H__
#define __BPM_UI_H__

#include <vector>

#include <SDK/foobar2000.h>

// The two windows the component puts up, as the rest of it sees them.
//
// Everything above this line is the same code on both platforms; everything
// below it is not. On Windows these are WTL dialogs built from the templates
// in foo_rubato.rc, and the implementations sit at the bottom of
// bpm_result_dialog.cpp and bpm_manual_dialog.cpp. On macOS they are Cocoa
// windows built in code, in mac/.
//
// Declaring them here rather than letting the callers name a dialog class is
// what keeps bpm_auto_analysis_thread.cpp and bpm_contextmenu_item.cpp - the
// two places a window is opened from - free of either platform's window
// system. Both must be called on the main thread.

//! The results window: one row per track, with the buttons that write the
//! tags. Takes its own copy of everything it is given.
//!
//! @param p_tracks      the tracks that were analysed, in the order to show them
//! @param p_infos       their file_info, already read, one per track
//! @param p_bpm_results the measured BPM per track
//! @param p_rhythms     the rhythm class per track, empty where none was found
//! @param p_spreads     how far the tempo moved, in BPM; 0 where unmeasurable
//! @param p_initial_bpms the tempo each track opens at; 0 where unmeasurable
void bpm_show_results(metadb_handle_list_cref p_tracks,
                      const pfc::list_t<file_info_impl> & p_infos,
                      const std::vector<double> & p_bpm_results,
                      const std::vector<pfc::string8> & p_rhythms,
                      const std::vector<double> & p_spreads,
                      const std::vector<double> & p_initial_bpms);

//! The manual tap window, which measures the tempo of whatever is playing from
//! the user tapping along with it.
void bpm_show_manual_tap();

#endif // __BPM_UI_H__
