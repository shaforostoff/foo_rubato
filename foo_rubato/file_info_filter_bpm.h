#ifndef __FILE_INFO_FILTER_BPM_H__
#define __FILE_INFO_FILTER_BPM_H__

#include <vector>

#include <SDK/foobar2000.h>

#include "bpm_track_result.h"

class file_info_filter_bpm : public file_info_filter
{
public:
	//! Results of the automatic analysis, one per track, in the same order.
	//!
	//! The BPM written carries an algorithm attribution unless the user
	//! doubled or halved it in the results window, which is what
	//! `bpm_track_result::adjusted` marks: the analysis no longer stands
	//! behind those, so they are treated like a hand-tapped value and lose it.
	//!
	//! `p_rhythm_tag` may be null when the rhythm is not to be written.
	file_info_filter_bpm(const metadb_handle_list & p_tracks,
	                     const char * p_bpm_tag,
	                     const std::vector<bpm_track_result> & p_results,
	                     const char * p_rhythm_tag = nullptr);
	//! One BPM the user tapped by hand. No analysis produced it, so any
	//! attribution already on the file is now false and is removed rather
	//! than written - see BPM_ALGORITHM_TAG. Nothing else on the file is
	//! touched: a hand tap says nothing about the key or the tuning, and a
	//! measurement already there is still true.
	file_info_filter_bpm(metadb_handle_ptr p_track, const char * p_bpm_tag, double p_bpm_result);
	bool apply_filter(metadb_handle_ptr p_track, t_filestats p_stats, file_info & p_info);

private:
	//! Writes `value` to `field`, or removes the field when `value` is empty.
	//!
	//! Removing is not the preferences page's to switch off, and that is the
	//! whole of why this is one function. Turning a field off means this
	//! component stops adding one, not that it starts leaving behind a value
	//! it knows no longer describes the file beside it. `enabled` governs
	//! writing; removal happens either way.
	static void set_or_remove(file_info & p_info, const char * field,
	                          const pfc::string8 & value, bool enabled);

	metadb_handle_list m_tracks;
	std::vector<bpm_track_result> m_results;
	pfc::string8 m_bpm_tag;
	pfc::string8 m_rhythm_tag;
	//! True for analysis results, false for a hand-tapped BPM.
	bool m_from_analysis;
	//! The preferences-page switches, read once when the filter is built
	//! rather than per track, so a setting changed while foobar2000 is part
	//! way through writing a selection cannot apply to only half of it.
	bool m_write_initial;
	bool m_write_algorithm;
	bool m_write_key;
	bool m_write_tuning;
	bool m_write_retune;
};

#endif // __FILE_INFO_FILTER_BPM_H__
