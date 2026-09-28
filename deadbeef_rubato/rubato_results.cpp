// What the windows show, decided once for every toolkit. See rubato_results.h.
//
// The parts that reach the player - reading the settings, releasing tracks,
// queuing writes - are in rubato_plugin.cpp beside the queue they use.

#include "rubato_results.h"
#include "rubato_format.h"

#include <algorithm>

namespace rubato
{

results::results(const settings & s, std::vector<row> rows)
	: m_settings(s), m_rows(std::move(rows))
{
	// The tempo figures read as one group, with the measurement next to the
	// tap it can be judged by. The tag column only when some track had a
	// tag, and key and tuning only when something was measured: permanently
	// empty columns would push the title out of the window for nothing.
	const bool have_tag = std::any_of(m_rows.begin(), m_rows.end(),
	                                  [](const row & r) { return !r.tag_bpm.empty(); });
	const bool have_key = std::any_of(m_rows.begin(), m_rows.end(),
	                                  [](const row & r) { return r.result.analysis.key.ok; });
	m_columns.push_back(column_title);
	m_columns.push_back(column_bpm);
	if (have_tag) m_columns.push_back(column_tag_bpm);
	m_columns.push_back(column_initial);
	m_columns.push_back(column_spread);
	m_columns.push_back(column_rhythm);
	if (have_key)
	{
		m_columns.push_back(column_key);
		m_columns.push_back(column_tuning);
	}
}

const char * results::heading(column_id c)
{
	switch (c)
	{
	case column_title:   return "Title";
	case column_bpm:     return "BPM";
	case column_tag_bpm: return "BPM from tag";
	case column_initial: return "Initial BPM";
	case column_spread:  return "Fluctuation";
	case column_rhythm:  return "Rhythm";
	case column_key:     return "Key";
	case column_tuning:  return "Tuning";
	}
	return "";
}

bool results::column_numeric(column_id c)
{
	return c == column_bpm || c == column_tag_bpm || c == column_initial
	    || c == column_spread || c == column_tuning;
}

std::string results::cell(std::size_t index, column_id c) const
{
	if (index >= m_rows.size()) return std::string();
	const row & r = m_rows[index];
	const track_result & t = r.result;
	const bool tempo = t.analysis.ok;
	switch (c)
	{
	case column_title:   return r.title;
	case column_bpm:     return tempo ? format_bpm(t.bpm, m_settings.precision) : std::string();
	// As the file carried it, decimal point and all.
	case column_tag_bpm: return r.tag_bpm;
	case column_initial:
		return tempo && t.initial_bpm > 0 ? format_bpm(t.initial_bpm, m_settings.precision)
		                                  : std::string();
	case column_spread:  return tempo ? format_spread(t.spread) : std::string();
	case column_rhythm:  return tempo ? bpmcore::rhythm_name(t.analysis.rhythm) : std::string();
	case column_key:     return format_key_column(t.analysis.key);
	case column_tuning:  return format_tuning_column(t.analysis.key);
	}
	return std::string();
}

std::string results::tooltip(std::size_t index, bool title_clipped) const
{
	if (index >= m_rows.size()) return std::string();
	const row & r = m_rows[index];
	return format_row_tooltip(r.title, title_clipped, r.result.analysis.key, r.result.year);
}

void results::scale(std::size_t index, double factor)
{
	if (index >= m_rows.size()) return;
	track_result & t = m_rows[index].result;
	if (!t.analysis.ok) return;
	// The fluctuation and the opening tempo are quoted at the level the BPM
	// is, so they follow the same factor.
	t.bpm *= factor;
	t.initial_bpm *= factor;
	t.spread *= factor;
	t.adjusted = true;
}

std::string results::commit_label() const
{
	return m_rows.size() == 1 ? "Update 1 file"
	                          : "Update " + std::to_string(m_rows.size()) + " files";
}

std::string results::destination() const
{
	return bpm_destination(m_settings);
}

std::string bpm_destination(const settings & s)
{
	if (equals_ascii_nocase(s.bpm_tag.c_str(), "BPM"))
		return "The BPM is written to BPM - TBPM in an mp3, tmpo in an m4a.";
	return "The BPM is written to " + s.bpm_tag + ".";
}

// --- tapping ---------------------------------------------------------------

tap_meter::tap_meter(const settings & s)
	: m_window(static_cast<std::size_t>(std::max(2, s.taps_to_average)))
	, m_reset(std::chrono::seconds(std::max(1, s.seconds_to_reset)))
	, m_precision(s.precision)
{
}

void tap_meter::tap(clock::time_point when)
{
	if (!m_taps.empty() && when - m_taps.back() > m_reset) m_taps.clear();
	if (m_taps.size() >= m_window) m_taps.erase(m_taps.begin());
	m_taps.push_back(when);
}

void tap_meter::reset()
{
	m_taps.clear();
}

double tap_meter::bpm() const
{
	if (m_taps.size() < 2) return 0;
	// The mean interval is the span over the number of intervals, so the
	// taps in between cancel out and only the first and last count - which
	// is the same answer as averaging each interval, arrived at directly.
	const double span = std::chrono::duration<double>(m_taps.back() - m_taps.front()).count();
	if (!(span > 0)) return 0;
	return 60.0 * static_cast<double>(m_taps.size() - 1) / span;
}

std::string tap_meter::text() const
{
	const double b = bpm();
	return b > 0 ? format_bpm(b, m_precision) : std::string();
}

}   // namespace rubato
