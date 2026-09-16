#include "stdafx.h"

#include "file_info_filter_scale_bpm.h"

#include "format_bpm.h"
#include "globals.h"

file_info_filter_scale_bpm::file_info_filter_scale_bpm(const char * p_bpm_tag, double p_scale)
	: m_bpm_tag(p_bpm_tag)
	, m_scale(p_scale)
{
}

bool file_info_filter_scale_bpm::apply_filter(metadb_handle_ptr p_track, t_filestats p_stats, file_info & p_info)
{
	const char * str = p_info.meta_get(m_bpm_tag, 0);

	// Plain sscanf rather than MSVC's sscanf_s: %f takes no buffer, so the two
	// differ in nothing here but which compilers have them.
	float bpm = 0.0f;
	if ((str != NULL) && (sscanf(str, "%f", &bpm) == 1))
	{
		bpm = static_cast<float>(bpm * m_scale);

		p_info.meta_set(m_bpm_tag, format_bpm(bpm));

		// Doubling or halving is the user overruling the measurement, so the
		// analysis no longer stands behind the value and its attribution goes.
		p_info.meta_remove_field(BPM_ALGORITHM_TAG);

		// The opening tempo is quoted at the same metrical level as the BPM, so
		// it is scaled rather than dropped: halving the BPM because the
		// analysis picked the wrong level means the opening was on the wrong
		// level too, and the same factor puts both right.
		const char * initial_str = p_info.meta_get(BPM_INITIAL_TAG, 0);
		float initial = 0.0f;
		if ((initial_str != NULL) && (sscanf(initial_str, "%f", &initial) == 1) && initial > 0)
		{
			p_info.meta_set(BPM_INITIAL_TAG,
			                format_bpm(static_cast<double>(initial) * m_scale));
		}

		return true;
	}
	else
	{
		return false;
	}
}
