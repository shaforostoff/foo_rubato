#ifndef __GLOBALS_H__
#define __GLOBALS_H__

#include <SDK/foobar2000.h>
#include <SDK/advconfig_impl.h>
#include "guid.h"

// The tag recording which analysis produced the BPM, and its version. Unlike
// the BPM and rhythm tag names this one is not configurable: it is an
// attribution rather than a place to put data, and a reader looking for it
// has to know what it is called. The value is FOO_RUBATO_ALGORITHM, from the
// generated version.h. Named to match the KeyAlgorithm and TuningAlgorithm
// fields other taggers write.
#define BPM_ALGORITHM_TAG "BpmAlgorithm"

// The tempo the track opens at, beside the BPM for the whole of it. Named
// after the INITIALKEY field other taggers write; foobar2000 chooses the
// spelling each container wants, which for INITIALKEY is upper case in a
// Vorbis comment, lower case in an iTunes freeform atom and the standard TKEY
// frame in ID3. Not configurable, for the same reason BPM_ALGORITHM_TAG is
// not: a reader has to know what it is called.
#define BPM_INITIAL_TAG "INITIALBPM"

// Config variables
// General
extern cfg_int bpm_config_bpm_precision;
extern cfg_string bpm_config_bpm_tag;
extern cfg_bool bpm_config_auto_write_tag;
extern cfg_bool bpm_config_write_initial_bpm;
extern cfg_bool bpm_config_write_bpm_algorithm;
// Diagnostics
extern cfg_bool bpm_config_output_debug;
// Manual
extern cfg_int bpm_config_taps_to_average;
extern cfg_int bpm_config_seconds_to_reset_average;

//! The tag the BPM is written to, as configured.
//!
//! Through a function rather than by reading bpm_config_bpm_tag directly,
//! because cfg_string is not one class. foobar2000-versions.h targets API 80
//! on Windows and API 81 on macOS, and cfg_var.h hands those two different
//! implementations: cfg_var_legacy::cfg_string *is* a pfc::string8 and can be
//! passed wherever one can, while cfg_var_modern::cfg_string keeps its value
//! behind get() and has neither get_ptr() nor the conversion. A pfc::string8
//! is what both will give, so that is what the rest of the component asks for.
//!
//! Each call reads the config afresh, so a value used more than once in a row
//! - a name and its length, say - wants a local rather than two calls.
pfc::string8 bpm_tag_name();

// Advanced preferences. None are registered at present - see the note in
// preferences.cpp for why, and for how to bring these two back.
//extern advconfig_checkbox_factory bpm_config_write_rhythm_tag;
//extern advconfig_string_factory bpm_config_rhythm_tag;

#endif