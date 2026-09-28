#ifndef RUBATO_RESULTS_H
#define RUBATO_RESULTS_H

// What the plugin's windows show and act on, with no window system in it.
//
// Everything a window needs to decide is decided here: which columns there
// are, what each cell says, what the pointer resting on a row says, what the
// double and halve buttons do to a row, what the commit button is labelled and
// what it writes, how far a scan has got, and what tapping along measures. A
// window is left with laying these out and passing clicks back - so the GTK
// windows in gtk/ and the Cocoa ones in cocoa/ cannot come to disagree about
// anything but their looks.
//
// rubato_ui.h is the other half: the functions the plugin calls to put a
// window up, one implementation per toolkit.

#ifndef DDB_API_LEVEL
#define DDB_API_LEVEL 10   // DeaDBeeF 1.8.0 and later
#endif
#include <deadbeef/deadbeef.h>

#include <chrono>
#include <cstddef>
#include <memory>
#include <string>
#include <vector>

#include <bpmcore/bpmcore.h>

namespace rubato
{

//! The preferences, read once when an action starts rather than per track, so
//! a setting changed while a selection is being written cannot apply to half
//! of it.
struct settings
{
	int precision = 0;
	std::string bpm_tag = "BPM";
	bool write_initial = true;
	bool write_algorithm = true;
	bool detect_key = true;
	bool write_key = true;
	bool write_tuning = true;
	bool write_retune = true;
	bool skip_tagged = false;
	bool diagnostics = false;
	//! Write without the results window; always the case with no window
	//! system to put one on.
	bool auto_write = false;
	int taps_to_average = 30;
	int seconds_to_reset = 5;
};

settings read_settings();

//! One track's analysis as it will be written. The tempo figures are copies
//! rather than readings of `analysis`, because the double and halve buttons
//! scale them and the analysis should go on saying what it measured.
struct track_result
{
	bpmcore::analysis analysis;
	double bpm = 0;
	double initial_bpm = 0;
	double spread = 0;
	int year = 0;
	//! Doubled or halved by the user, so the analysis no longer stands behind
	//! the number and it loses its attribution.
	bool adjusted = false;
};

enum column_id
{
	column_title,
	column_bpm,
	column_tag_bpm,   //!< only when some track arrived with a BPM
	column_initial,
	column_spread,
	column_rhythm,
	column_key,       //!< only when some track's key was measured
	column_tuning
};

//! A finished scan, waiting for the user to write it or not.
//!
//! Owned by the window showing it, through a shared_ptr, and not thread-safe:
//! everything here is called on the window system's thread.
class results
{
public:
	struct row
	{
		DB_playItem_t * track = nullptr;   //!< holds a reference
		std::string title;
		std::string path;
		//! What the BPM field held before the scan, exactly as the file
		//! carried it: on a collection of hand taps, the figure to judge the
		//! measurement by.
		std::string tag_bpm;
		track_result result;
	};

	results(const settings & s, std::vector<row> rows);
	~results();
	results(const results &) = delete;
	results & operator=(const results &) = delete;

	std::size_t size() const { return m_rows.size(); }
	const std::vector<column_id> & columns() const { return m_columns; }

	static const char * heading(column_id c);
	//! Numbers, which read best right-aligned.
	static bool column_numeric(column_id c);

	std::string cell(std::size_t row, column_id c) const;
	//! What the pointer resting on `row` says, or blank for nothing. The
	//! window says whether it is drawing the title cut short, which is the
	//! only case in which repeating the title tells the reader anything.
	std::string tooltip(std::size_t row, bool title_clipped) const;

	//! Doubles or halves one row's tempo figures. The key and the tuning are
	//! not tempo measurements and stay as they are.
	void scale(std::size_t row, double factor);

	//! "Update 12 files": the button writes every row, not the selection,
	//! and says so.
	std::string commit_label() const;
	//! Where the BPM goes, said once under the list.
	std::string destination() const;

	//! Writes every row, in the background. Once only: the window closes.
	void commit();

private:
	settings m_settings;
	std::vector<row> m_rows;
	std::vector<column_id> m_columns;
	bool m_committed = false;
};

//! How a scan is getting on, for a progress window to poll. Thread-safe.
class scan_progress
{
public:
	struct snapshot
	{
		std::size_t done = 0;
		std::size_t total = 0;
		//! Over the whole scan, with the tracks in flight counted by how far
		//! through each one is.
		double fraction = 0;
		//! "a.flac, b.flac and 4 more": the tracks being read right now.
		std::string in_flight;
		bool finished = false;
	};

	virtual ~scan_progress() {}
	virtual snapshot read() const = 0;
	//! Abandons what is left. A track half analysed is not written, and a
	//! scan waiting to be reviewed puts up no window.
	virtual void cancel() = 0;
};

//! Tempo from the user tapping along. No clock of its own to keep: the
//! window hands in the moment of each tap.
class tap_meter
{
public:
	using clock = std::chrono::steady_clock;

	explicit tap_meter(const settings & s);

	//! A pause longer than the reset time starts the average again, the tap
	//! that ended it being the first of the new run.
	void tap(clock::time_point when);
	void reset();

	//! 0 until there are two taps.
	double bpm() const;
	std::size_t taps() const { return m_taps.size(); }
	//! The BPM as it would be written: in the configured precision.
	std::string text() const;

private:
	std::vector<clock::time_point> m_taps;
	std::size_t m_window;
	clock::duration m_reset;
	int m_precision;
};

//! Where the BPM goes, as the results and tapping windows say it.
std::string bpm_destination(const settings & s);

//! The title of whatever is playing, or blank for nothing.
std::string playing_title();

//! Writes a tapped BPM to whatever is playing, in the background. False, with
//! nothing written, when nothing is. A hand tap says nothing about the key,
//! the tuning or the opening tempo: the BPM is written, INITIALBPM and the
//! BPM's attribution are removed, and the rest is left alone.
bool write_tapped_bpm(double bpm);

}   // namespace rubato

#endif // RUBATO_RESULTS_H
