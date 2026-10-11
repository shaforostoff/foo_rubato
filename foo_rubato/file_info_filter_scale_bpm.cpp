#include "stdafx.h"

#include "file_info_filter_scale_bpm.h"

#include "format_bpm.h"
#include "globals.h"

#include <deadbeef_rubato/rubato_format.h>

file_info_filter_scale_bpm::file_info_filter_scale_bpm(const char * p_bpm_tag, double p_scale)
	: m_bpm_tag(p_bpm_tag)
	, m_scale(p_scale)
{
}

bool file_info_filter_scale_bpm::apply_filter(metadb_handle_ptr p_track, t_filestats p_stats, file_info & p_info)
{
	const char * str = p_info.meta_get(m_bpm_tag, 0);

	// rubato::parse_decimal rather than sscanf, which stops at a comma: a tag
	// holding "120,5" doubled to 240 instead of 241.
	double bpm = 0.0;
	if (rubato::parse_decimal(str, bpm))
	{
		p_info.meta_set(m_bpm_tag, format_bpm(bpm * m_scale));

		// Doubling or halving is the user overruling the measurement, so the
		// analysis no longer stands behind the value and its attribution goes.
		//
		// The BPM's attribution, and only that one. KeyAlgorithm is left where
		// it is: a metrical level corrected by hand says nothing about the key
		// beside it, which is still the key the analysis measured, and this is
		// the whole reason the two attributions are separate fields.
		p_info.meta_remove_field(BPM_ALGORITHM_TAG);

		// The opening tempo is quoted at the same metrical level as the BPM, so
		// it is scaled rather than dropped: halving the BPM because the
		// analysis picked the wrong level means the opening was on the wrong
		// level too, and the same factor puts both right.
		double initial = 0.0;
		if (rubato::parse_decimal(p_info.meta_get(BPM_INITIAL_TAG, 0), initial) && initial > 0)
			p_info.meta_set(BPM_INITIAL_TAG, format_bpm(initial * m_scale));

		return true;
	}
	else
	{
		return false;
	}
}
