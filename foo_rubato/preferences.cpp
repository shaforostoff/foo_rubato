#include "stdafx.h"
#include "preferences.h"

// General
cfg_int bpm_config_bpm_precision(guid_bpm_config_bpm_precision, BPM_PRECISION_1);
cfg_string bpm_config_bpm_tag(guid_bpm_config_bpm_tag, "BPM");
cfg_bool bpm_config_auto_write_tag(guid_bpm_config_auto_write_tag, false);
// Both on by default: a BPM this component measured should say so, and the
// tempo a side opens at is one of the things it was taught to measure. Off is
// for someone who wants nothing in their files but the BPM itself.
cfg_bool bpm_config_write_initial_bpm(guid_bpm_config_write_initial_bpm, true);
cfg_bool bpm_config_write_bpm_algorithm(guid_bpm_config_write_bpm_algorithm, true);
// Diagnostics
cfg_bool bpm_config_output_debug(guid_bpm_config_output_debug, false);
// Manual
cfg_int bpm_config_taps_to_average(guid_bpm_config_taps_to_average, 30);
cfg_int bpm_config_seconds_to_reset_average(guid_bpm_config_seconds_to_reset_average, 5);

pfc::string8 bpm_tag_name()
{
	// One expression for both: the legacy class slices to its pfc::string8
	// base, the modern one goes through its operator pfc::string8().
	return bpm_config_bpm_tag;
}

// Advanced preferences - none at present, so the branch is not registered
// either: an empty node under Preferences > Advanced > Tools would be worse
// than no node. The switch that used to live here selected the legacy 2009
// engine, which is gone.
//
// An advanced-config entry has no way to register itself and stay out of the
// tree - the flags on these factories say whether a change needs a restart and
// what a string holds, nothing about visibility - so not registering is what
// hiding amounts to. The rhythm entries below are commented out alongside the
// tag writing itself, in rhythm_tag_or_empty in bpm_result_dialog.cpp, and are
// declared in globals.h commented out to match. Restoring them means restoring
// the branch with them.
//
// None of these settings is lost meanwhile. They persist into foobar2000's own
// configuration keyed by GUID, untouched by the absence of the code that reads
// them, so whatever they held comes back when the code does.
//
//static advconfig_branch_factory bpm_config_branch(
//	"Rubato BPM Analyzer", guid_bpm_advconfig_branch, advconfig_branch::guid_branch_tools, 0);
//
//advconfig_checkbox_factory bpm_config_write_rhythm_tag(
//	"Write the detected rhythm to a tag", guid_bpm_config_write_rhythm_tag,
//	guid_bpm_advconfig_branch, 1, true);
//
//advconfig_string_factory bpm_config_rhythm_tag(
//	"Rhythm tag name", guid_bpm_config_rhythm_tag,
//	guid_bpm_advconfig_branch, 2, "RHYTHM");
