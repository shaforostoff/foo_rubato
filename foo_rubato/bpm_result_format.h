#ifndef __BPM_RESULT_FORMAT_H__
#define __BPM_RESULT_FORMAT_H__

#include <pfc/pfc.h>

#include "format_bpm.h"

// How the generated columns of the results window are written out.
//
// Shared rather than private to one window because there are two results
// windows - the WTL one and the Cocoa one - and a column that rounded
// differently between them would be a difference in what the component
// reports, not in how it draws.

//! The fluctuation column: how far the tempo moves over the track, as a
//! plus-or-minus in BPM. Blank where bpmcore could not measure one, which it
//! reports as zero - a track under about half a minute has too few windows to
//! draw a spread from.
//!
//! One decimal: the figure is a description of a performance, not a
//! measurement to carry around, and the second decimal is noise.
inline pfc::string8 bpm_format_spread(double bpm_spread)
{
	pfc::string8 out;
	// U+00B1, spelt out so the file's own encoding cannot come into it.
	if (bpm_spread > 0) out << "\xc2\xb1" << pfc::format_float(bpm_spread, 0, 1);
	return out;
}

//! The opening-tempo column, blank where there was no beat near the start to
//! measure one from. Formatted like the BPM column beside it, so the two can be
//! read against each other at a glance.
inline pfc::string8 bpm_format_initial(double initial_bpm)
{
	pfc::string8 out;
	if (initial_bpm > 0) out << format_bpm(initial_bpm).get_ptr();
	return out;
}

//! What the BPM tag held before the scan. Shown as the file carried it,
//! including any decimal point - on this collection a whole number is a hand
//! tap and a decimal is machine-written, and that distinction is worth more
//! than a tidy column.
inline pfc::string8 bpm_format_tag_bpm(const pfc::string8 & tag_bpm)
{
	return tag_bpm;
}

//! The rhythm tag, or an empty string when writing it is switched off.
//!
//! Writing it is switched off outright at the moment: the empty string stops
//! file_info_filter_bpm from touching the tag at all. The detected rhythm is
//! still shown in the results window, and the advanced-config entries that used
//! to control this are still there and now do nothing - restore the two lines
//! below to give them their effect back.
inline pfc::string8 bpm_rhythm_tag_or_empty()
{
	pfc::string8 tag;
//	if (!bpm_config_write_rhythm_tag.get()) return tag;
//	bpm_config_rhythm_tag.get(tag);
	return tag;
}

#endif // __BPM_RESULT_FORMAT_H__
