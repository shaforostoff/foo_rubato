#ifndef __GLOBALS_H__
#define __GLOBALS_H__

#include <SDK/foobar2000.h>
#include <SDK/advconfig_impl.h>
#include "guid.h"

// The tags recording which analysis produced a measurement, and its version.
// Unlike the BPM and rhythm tag names these are not configurable: they are an
// attribution rather than a place to put data, and a reader looking for one
// has to know what it is called. Both carry FOO_RUBATO_ALGORITHM, from the
// generated version.h - one component and one version, whichever of the two
// measurements is being stamped. Named to match the KeyAlgorithm and
// TuningAlgorithm fields other taggers write.
//
// Two fields rather than one because the two measurements are overruled
// separately. Doubling a BPM in the results window says nothing about the key
// beside it, and a track with no steady pitch in it still produced a BPM worth
// attributing, so one attribution covering both would be wrong about one of
// them every time either is.
#define BPM_ALGORITHM_TAG     "BpmAlgorithm"
#define BPM_KEY_ALGORITHM_TAG "KeyAlgorithm"
// TUNING's attribution. beaTunes writes TUNING (as "Tuning") with one of these
// beside it, and foobar2000 treats the two spellings as one field, so writing
// our TUNING without touching this left files crediting beaTunes for our
// number.
#define BPM_TUNING_ALGORITHM_TAG "TuningAlgorithm"

// The tempo the track opens at, beside the BPM for the whole of it. Named
// after the INITIALKEY field other taggers write. foobar2000 writes it under
// exactly this name in every container - a TXXX frame in ID3, a freeform atom
// in MP4 - which is fine for a field only this component defines. Not
// configurable, for the same reason BPM_ALGORITHM_TAG is not: a reader has to
// know what it is called.
#define BPM_INITIAL_TAG "INITIALBPM"

// Tuning and key. Not configurable either, for the same reason: a reader has
// to know what these are called, and TUNING in particular is the field
// beaTunes already writes in cents, so the two can sit in one library without
// either having to be told about the other.
//
// KEY holds one answer because that is what a player or a DJ tool will read -
// and so does the container's standard key slot, which is the copy most of
// them actually find: TKEY in ID3, "initialkey" in MP4, INITIALKEY in a Vorbis
// comment. Which field name reaches which slot is bpm_initial_key_field's
// business, in bpm_tag_fields.h. KEY stays as well, so files tagged before the
// slot was written keep a field that means the same thing.
//
// KEYCANDIDATES holds all three with their scores, which is where the rest of
// what was measured is: the first is right 79% of the time and the true key
// is in the three 95% of the time. KEYCONFIDENCE says which of those
// two numbers applies to this track.
#define BPM_KEY_TAG              "KEY"
#define BPM_KEY_CANDIDATES_TAG   "KEYCANDIDATES"
#define BPM_KEY_CONFIDENCE_TAG   "KEYCONFIDENCE"
#define BPM_MODE_BALANCE_TAG     "MODEBALANCE"
#define BPM_TUNING_TAG           "TUNING"
#define BPM_RETUNE_TAG           "RETUNE"
#define BPM_RETUNE_CANDIDATES_TAG "RETUNECANDIDATES"

// Config variables
// General
extern cfg_int bpm_config_bpm_precision;
extern cfg_string bpm_config_bpm_tag;
extern cfg_bool bpm_config_auto_write_tag;
extern cfg_bool bpm_config_write_initial_bpm;
extern cfg_bool bpm_config_write_bpm_algorithm;
// Tuning and key
extern cfg_bool bpm_config_detect_key;
extern cfg_bool bpm_config_write_key;
extern cfg_bool bpm_config_write_tuning;
extern cfg_bool bpm_config_write_retune;
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