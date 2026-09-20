#include "stdafx.h"

#include "file_info_filter_bpm.h"

#include "bpm_result_format.h"
#include "format_bpm.h"
#include "globals.h"
#include "version.h"

file_info_filter_bpm::file_info_filter_bpm(const metadb_handle_list & p_tracks,
                                           const char * p_bpm_tag,
                                           const std::vector<bpm_track_result> & p_results,
                                           const char * p_rhythm_tag)
	: m_bpm_tag(p_bpm_tag), m_rhythm_tag(p_rhythm_tag != nullptr ? p_rhythm_tag : "")
	, m_from_analysis(true)
	, m_write_initial(bpm_config_write_initial_bpm)
	, m_write_algorithm(bpm_config_write_bpm_algorithm)
	, m_write_key(bpm_config_write_key)
	, m_write_tuning(bpm_config_write_tuning)
	, m_write_retune(bpm_config_write_retune)
{
	pfc::dynamic_assert(p_tracks.get_count() == p_results.size());
	pfc::array_t<t_size> order;
	order.set_size(p_tracks.get_count());
	order_helper::g_fill(order.get_ptr(), order.get_size());
	p_tracks.sort_get_permutation_t(pfc::compare_t<metadb_handle_ptr, metadb_handle_ptr>, order.get_ptr());

	// The tracks are sorted so apply_filter can bsearch them, and the results
	// follow the same permutation. One array to keep in step rather than six,
	// which is what the result struct is for.
	m_tracks.set_count(order.get_size());
	m_results.resize(order.get_size());
	for (t_size n = 0; n < order.get_size(); n++)
	{
		m_tracks[n] = p_tracks[order[n]];
		m_results[n] = p_results[order[n]];
	}
}

file_info_filter_bpm::file_info_filter_bpm(metadb_handle_ptr p_track, const char * p_bpm_tag, double p_bpm_result)
	: m_bpm_tag(p_bpm_tag)
	, m_from_analysis(false)
	, m_write_initial(bpm_config_write_initial_bpm)
	, m_write_algorithm(bpm_config_write_bpm_algorithm)
	, m_write_key(false)
	, m_write_tuning(false)
	, m_write_retune(false)
{
	m_tracks.add_item(p_track);
	m_results.resize(1);
	m_results[0].bpm = p_bpm_result;
}

void file_info_filter_bpm::set_or_remove(file_info & p_info, const char * field,
                                         const pfc::string8 & value, bool enabled)
{
	if (value.is_empty() || !enabled) p_info.meta_remove_field(field);
	else p_info.meta_set(field, value);
}

bool file_info_filter_bpm::apply_filter(metadb_handle_ptr p_track, t_filestats p_stats, file_info & p_info)
{
	t_size index;
	if (!m_tracks.bsearch_t(pfc::compare_t<metadb_handle_ptr, metadb_handle_ptr>, p_track, index))
		return false;

	const bpm_track_result & r = m_results[index];

	format_bpm bpm_value(r.bpm);
	p_info.meta_set(m_bpm_tag, bpm_value);

	// Record what produced the number, or clear an attribution that is no
	// longer true. Only a BPM the analysis stands behind is stamped: for one
	// tapped by hand, or doubled or halved by the user, an attribution left
	// over from an earlier scan would be claiming credit for a number the
	// analysis did not produce.
	//
	// The clearing is not the preferences page's to switch off. Turning the
	// attribution off means this component stops adding one, not that it
	// starts leaving behind a claim it knows to be false - which is the whole
	// value of the field, its presence being what marks a BPM as measured
	// rather than tapped.
	const bool ours = m_from_analysis && !r.adjusted;
	if (!ours) p_info.meta_remove_field(BPM_ALGORITHM_TAG);
	else if (m_write_algorithm) p_info.meta_set(BPM_ALGORITHM_TAG, FOO_RUBATO_ALGORITHM);

	// The tempo the track opens at, which on a side that eases off for the
	// singer is several BPM above the figure for the whole of it.
	pfc::string8 initial;
	if (m_from_analysis && r.initial_bpm > 0) initial = format_bpm(r.initial_bpm);
	set_or_remove(p_info, BPM_INITIAL_TAG, initial, m_write_initial);

	if (!m_rhythm_tag.is_empty() && !r.rhythm.is_empty())
		p_info.meta_set(m_rhythm_tag, r.rhythm);

	// A hand tap says nothing about the key or the tuning, so it leaves both
	// alone rather than clearing measurements that are still true. Everything
	// below this line is for an analysis result only.
	if (!m_from_analysis) return true;

	// The key, and - always beside it - how far to trust it. KEY on its own
	// reads as a fact; it is right 60% of the time, and the two fields that
	// say so travel with it or none of them is written.
	const pfc::string8 key_name = bpm_format_key(r.key);
	set_or_remove(p_info, BPM_KEY_TAG, key_name, m_write_key);
	set_or_remove(p_info, BPM_KEY_CANDIDATES_TAG,
	              bpm_format_key_candidates(r.key), m_write_key);
	set_or_remove(p_info, BPM_KEY_CONFIDENCE_TAG,
	              bpm_format_key_confidence(r.key), m_write_key);
	set_or_remove(p_info, BPM_MODE_BALANCE_TAG,
	              bpm_format_mode_balance(r.key), m_write_key);

	// The key's own attribution, on the same terms as the BPM's above: stamped
	// only where this component stands behind the value beside it, and removed
	// rather than left standing anywhere it does not.
	//
	// What it stands behind here is a KEY this scan actually wrote. A track
	// with no steady pitch in it measures no key, and with the key fields
	// switched off none is written, and in both cases KEY has just been removed
	// - so an attribution for it would be claiming credit either for a field
	// that is not there or for one some other tagger put there.
	const bool key_ours = m_write_key && !key_name.is_empty();
	if (!key_ours) p_info.meta_remove_field(BPM_KEY_ALGORITHM_TAG);
	else if (m_write_algorithm) p_info.meta_set(BPM_KEY_ALGORITHM_TAG, FOO_RUBATO_ALGORITHM);

	set_or_remove(p_info, BPM_TUNING_TAG, bpm_format_tuning(r.key), m_write_tuning);

	set_or_remove(p_info, BPM_RETUNE_TAG,
	              bpm_format_retune(r.key, r.year), m_write_retune);
	set_or_remove(p_info, BPM_RETUNE_CANDIDATES_TAG,
	              bpm_format_retune_candidates(r.key, r.year), m_write_retune);

	return true;
}
