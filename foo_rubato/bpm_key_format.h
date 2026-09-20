#ifndef __BPM_KEY_FORMAT_H__
#define __BPM_KEY_FORMAT_H__

#include <cmath>

#include <pfc/pfc.h>

#include <bpmcore/bpmcore.h>

// How the tuning and key measurements are written out, for both the tags and
// the results window.
//
// Split from bpm_result_format.h, which reaches the preferences page through
// format_bpm.h and so cannot be built without the whole foobar2000 SDK. These
// need pfc and bpmcore and nothing else, which is what lets foo_rubato_test
// check the exact strings that reach people's files - and those strings are
// the deliverable here, not an implementation detail.
//
// Every one of these returns an empty string where there is nothing to say,
// and every caller treats an empty string as "do not write this tag, and
// remove one already there". A field that survives a rescan it no longer
// describes is worse than a missing field.

//! A signed number with a fixed number of decimals.
//!
//! pfc::format_float never writes a leading plus, and these figures are read
//! as corrections, where the direction is most of the meaning.
inline pfc::string8 bpm_format_signed(double value, unsigned precision)
{
	pfc::string8 out;
	// Guarded against a value that rounds to zero from below: "-0.00%" is a
	// correction in a direction, and there isn't one.
	const double rounded = std::floor(value * std::pow(10.0, (double) precision) + 0.5);
	if (rounded >= 0) out << "+";
	out << pfc::format_float(rounded == 0 ? 0.0 : value, 0, precision);
	return out;
}

//! Cents from A=440, to one decimal. Blank when the measurement is too
//! uncertain to mean anything, which is a track with no stable pitch in it.
inline pfc::string8 bpm_format_tuning(const bpmcore::key_analysis & key)
{
	pfc::string8 out;
	if (key.ok && key.tuning_ok) out << pfc::format_float(key.tuning_cents, 0, 1);
	return out;
}

//! The single best key, or blank.
inline pfc::string8 bpm_format_key(const bpmcore::key_analysis & key)
{
	pfc::string8 out;
	if (key.ok && key.best.root >= 0)
		out << bpmcore::key_name(key.best.root, key.best.minor);
	return out;
}

//! All three candidates with their correlations - "Dm:0.866 C:0.702 Gm:0.701".
//!
//! Not a consolation prize for the ones that lost. The first is right 60% of
//! the time and the true key is somewhere in the three 93% of the time, so
//! this is where most of what was measured actually is.
inline pfc::string8 bpm_format_key_candidates(const bpmcore::key_analysis & key)
{
	pfc::string8 out;
	if (!key.ok) return out;
	for (int i = 0; i < key.candidate_count; i++)
	{
		if (key.candidates[i].root < 0) continue;
		if (!out.is_empty()) out << " ";
		out << bpmcore::key_name(key.candidates[i].root, key.candidates[i].minor)
		    << ":" << pfc::format_float(key.candidates[i].score, 0, 3);
	}
	return out;
}

inline pfc::string8 bpm_format_key_confidence(const bpmcore::key_analysis & key)
{
	pfc::string8 out;
	if (key.ok) out << bpmcore::key_confidence_name(key.confidence);
	return out;
}

//! "40% major, 7 switches" - which of the relative pair held the track, and
//! how often it changed hands. Blank on a track too short to track.
inline pfc::string8 bpm_format_mode_balance(const bpmcore::key_analysis & key)
{
	pfc::string8 out;
	if (!key.ok || key.major_fraction < 0) return out;
	out << pfc::format_float(100.0 * key.major_fraction, 0, 0) << "% major, "
	    << key.mode_switches << (key.mode_switches == 1 ? " switch" : " switches");
	return out;
}

//! "-1.72% to A=440", or blank where no suggestion should be made - which is
//! an unknown year, or one from 1976 on, when everything was cut at A=440 and
//! a transfer that is off is off for reasons this cannot see.
inline pfc::string8 bpm_format_retune(const bpmcore::key_analysis & key, int year)
{
	pfc::string8 out;
	bpmcore::retune_option opt[bpmcore::key_candidate_count];
	if (!key.ok || !key.tuning_ok) return out;
	if (bpmcore::suggest_retune(key.tuning_cents, year, opt,
	                            bpmcore::key_candidate_count) < 1) return out;
	out << bpm_format_signed(opt[0].percent, 2) << "% to "
	    << bpmcore::retune_target_name(opt[0].target);
	return out;
}

//! Every candidate - "-1.72%@A=440 -2.83%@A=435 +2.94%@A=435".
//!
//! There is more than one because the offset is only known modulo a semitone,
//! and because in the transition years both reference pitches were in use.
//! Blank when there is only the one, which would just repeat RETUNE.
inline pfc::string8 bpm_format_retune_candidates(const bpmcore::key_analysis & key, int year)
{
	pfc::string8 out;
	bpmcore::retune_option opt[bpmcore::key_candidate_count];
	if (!key.ok || !key.tuning_ok) return out;
	const int n = bpmcore::suggest_retune(key.tuning_cents, year, opt,
	                                      bpmcore::key_candidate_count);
	if (n < 2) return out;
	for (int i = 0; i < n; i++)
	{
		if (i != 0) out << " ";
		out << bpm_format_signed(opt[i].percent, 2) << "%@"
		    << bpmcore::retune_target_name(opt[i].target);
	}
	return out;
}

//! The key column of the results window: the answer with how far to trust it.
//!
//! The confidence rides along rather than sitting in its own column because
//! the two are never read apart - "Dm" alone invites more belief than the
//! number behind it supports.
inline pfc::string8 bpm_format_key_column(const bpmcore::key_analysis & key)
{
	pfc::string8 out = bpm_format_key(key);
	if (out.is_empty()) return out;
	out << " (" << bpmcore::key_confidence_name(key.confidence) << ")";
	return out;
}

//! The tuning column: cents, and a mark where the value is not to be trusted.
inline pfc::string8 bpm_format_tuning_column(const bpmcore::key_analysis & key)
{
	pfc::string8 out;
	if (!key.ok) return out;
	if (!key.tuning_ok) { out << "?"; return out; }
	out << bpm_format_signed(key.tuning_cents, 1) << " c";
	// Within 5 cents of the wrap the pitch-class binning becomes unstable and
	// the key can land a semitone out, so say so where it can be seen.
	if (key.near_wrap) out << " (!)";
	return out;
}

#endif // __BPM_KEY_FORMAT_H__
