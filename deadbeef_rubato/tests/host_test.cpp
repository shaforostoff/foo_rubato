// The plugin driven through a stand-in for DeaDBeeF (fake_host.h), with a
// window system that records what it is asked to show (ui_recorder.h).
//
// Runs the real actions on the real worker threads: every track decoded,
// analysed and written the way the player would see it. The first half runs
// with no windows, as a build without RUBATO_DDB_GTK does; the second with
// them, through the results window's model and the tapping window's, which is
// everything a window decides.

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "fake_host.h"
#include "rubato_results.h"
#include "rubato_version.h"
#include "ui_recorder.h"

using namespace fake;

namespace
{

int failures = 0;

void check(bool ok, const char * what)
{
	std::printf("%s %s\n", ok ? "ok  " : "FAIL", what);
	if (!ok) failures++;
}

//! The column a model shows `id` in, or -1.
int column_of(const rubato::results & r, rubato::column_id id)
{
	for (std::size_t c = 0; c < r.columns().size(); c++)
		if (r.columns()[c] == id) return static_cast<int>(c);
	return -1;
}

}   // namespace

int main()
{
	init();

	fake_track flac, mp3, cue, other;
	flac.meta = { { ":URI", "/music/tango.flac" }, { "year", "1941-05-02" } };
	mp3.meta = { { ":URI", "/music/song.mp3" }, { "BPM", "99" },
	             { "INITIAL_KEY", "F#m" }, { "KeyAlgorithm", "beaTunes;v=5" } };
	mp3.rate = 48000;
	mp3.int16 = true;
	mp3.click_bpm = 100;
	cue.meta = { { ":URI", "/music/album.flac" } };
	cue.subtrack = true;
	other.meta = { { ":URI", "/music/other.flac" } };
	other.selected = false;
	playlist = { &flac, &mp3, &cue, &other };

	conf_ints["rubato.bpm_precision"] = 2;
	conf_ints["rubato.diagnostics"] = 1;

	DB_functions_t api = make_api();
	DB_plugin_t * p = ddb_rubato_load(&api);
	check(p != nullptr && p->type == DB_PLUGIN_MISC, "plugin loads as a misc plugin");
	check(p->api_vmajor == 1 && p->api_vminor == 10, "asks for API 1.10");
	check(p->configdialog != nullptr && std::strstr(p->configdialog, "rubato.bpm_tag") != nullptr,
	      "has a settings dialog");
	check(p->start() == 0 && p->connect() == 0, "starts and connects");

	DB_plugin_action_t * analyse = find_action(p, "rubato_analyse");
	DB_plugin_action_t * analyse_only = find_action(p, "rubato_analyse_only");
	DB_plugin_action_t * doubler = find_action(p, "rubato_double");
	DB_plugin_action_t * halver = find_action(p, "rubato_halve");
	check(analyse && analyse_only && doubler && halver, "without windows: four actions");
	check(find_action(p, "rubato_tap") == nullptr, "without windows: no tapping window offered");
	if (!(analyse && analyse_only && doubler && halver)) return 1;

	// --- analyse and write ---
	analyse->callback2(analyse, DDB_ACTION_CTX_SELECTION);
	rubato::wait_until_idle();

	std::printf("flac: BPM %s, INITIALBPM %s, KEY %s, TUNING %s, RETUNE %s\n",
	            get(flac, "BPM").c_str(), get(flac, "INITIALBPM").c_str(),
	            get(flac, "KEY").c_str(), get(flac, "TUNING").c_str(), get(flac, "RETUNE").c_str());
	check(recorder::progress_shown == 0 && recorder::results.empty(), "no window asked for");
	check(flac.writes == 1, "flac written once");
	check(near_level(number(flac, "BPM"), 120), "flac BPM at a level of the 120 BPM click");
	check(get(flac, "BPM").find('.') == get(flac, "BPM").size() - 3, "flac BPM has two decimals");
	check(get(flac, "BpmAlgorithm") == std::string("Rubato;v=") + RUBATO_VERSION,
	      "flac BpmAlgorithm stamped");
	check(!get(flac, "INITIALBPM").empty(), "flac INITIALBPM written");
	check(get(flac, "KEY").empty() || get(flac, "INITIALKEY") == get(flac, "KEY"),
	      "flac key slot is INITIALKEY");
	check(get(flac, "KEY").empty() || !get(flac, "KEYCONFIDENCE").empty(),
	      "flac KEY never travels alone");

	std::printf("mp3: BEATS_PER_MINUTE %s, INITIAL_KEY %s, KeyAlgorithm %s\n",
	            get(mp3, "BEATS_PER_MINUTE").c_str(), get(mp3, "INITIAL_KEY").c_str(),
	            get(mp3, "KeyAlgorithm").c_str());
	check(mp3.writes == 1, "mp3 written once");
	check(near_level(number(mp3, "BEATS_PER_MINUTE"), 100), "mp3 BPM in TBPM, from 16 bit audio");
	check(get(mp3, "BPM").empty(), "mp3 TXXX:BPM removed beside TBPM");
	const bool took_slot = get(mp3, "INITIAL_KEY") == get(mp3, "KEY")
	                    && get(mp3, "KeyAlgorithm").compare(0, 7, "Rubato;") == 0;
	const bool left_slot = get(mp3, "KEY").empty() && get(mp3, "INITIAL_KEY") == "F#m"
	                    && get(mp3, "KeyAlgorithm") == "beaTunes;v=5";
	check(took_slot || left_slot, "mp3 TKEY and its attribution move together");

	check(cue.writes == 0 && get(cue, "BPM").empty(), "subtrack left alone");
	check(other.writes == 0 && get(other, "BPM").empty(), "unselected track left alone");
	check(events == 2, "a track-changed event for each written track");
	check(saves == 1, "playlists saved once for the batch");

	// --- double, then halve back ---
	const double before = number(flac, "BPM");
	const double initial_before = number(flac, "INITIALBPM");
	flac.selected = true; mp3.selected = false; cue.selected = false;
	doubler->callback2(doubler, DDB_ACTION_CTX_SELECTION);
	rubato::wait_until_idle();
	check(std::fabs(number(flac, "BPM") - 2 * before) < 0.011, "double doubles the BPM");
	check(std::fabs(number(flac, "INITIALBPM") - 2 * initial_before) < 0.011,
	      "double doubles INITIALBPM");
	check(get(flac, "BpmAlgorithm").empty(), "a doubled BPM loses its attribution");
	check(get(flac, "KEY").empty() || !get(flac, "KeyAlgorithm").empty(),
	      "the key keeps its attribution");
	halver->callback2(halver, DDB_ACTION_CTX_SELECTION);
	rubato::wait_until_idle();
	check(std::fabs(number(flac, "BPM") - before) < 0.011, "halve undoes it");
	check(flac.writes == 3, "each scaling written");

	// --- look without writing ---
	flac.selected = true; mp3.selected = true;
	analyse_only->callback2(analyse_only, DDB_ACTION_CTX_SELECTION);
	rubato::wait_until_idle();
	check(flac.writes == 3 && mp3.writes == 1, "analyse without writing writes nothing");

	// --- the whole playlist, skipping what is tagged ---
	conf_ints["rubato.skip_tagged"] = 1;
	analyse->callback2(analyse, DDB_ACTION_CTX_PLAYLIST);
	rubato::wait_until_idle();
	check(flac.writes == 3 && mp3.writes == 1, "tagged tracks skipped");
	check(other.writes == 1 && !get(other, "BPM").empty(), "playlist context reaches unselected");
	conf_ints["rubato.skip_tagged"] = 0;

	// ===================== with windows =====================
	recorder::available = true;
	check(find_action(p, "rubato_tap") != nullptr, "with windows: tapping window offered");
	check(find_action(p, "rubato_analyse_only") == nullptr,
	      "with windows: the results window stands in for analysing without writing");

	// --- a scan goes to the results window, and nothing is written ---
	flac.selected = true; mp3.selected = true; cue.selected = false; other.selected = false;
	const int flac_writes = flac.writes, mp3_writes = mp3.writes;
	analyse->callback2(analyse, DDB_ACTION_CTX_SELECTION);
	rubato::wait_until_idle();
	check(recorder::progress_shown == 1, "a progress window asked for");
	check(recorder::last_progress && recorder::last_progress->read().finished
	      && recorder::last_progress->read().done == 2, "progress reads finished, 2 of 2");
	check(recorder::results.size() == 1 && recorder::results[0]->size() == 2,
	      "one results window, two rows");
	check(flac.writes == flac_writes && mp3.writes == mp3_writes, "nothing written before the button");
	if (recorder::results.size() != 1) return 1;

	rubato::results & r = *recorder::results[0];
	check(r.commit_label() == "Update 2 files", "the button counts the files");
	const int bpm_col = column_of(r, rubato::column_bpm);
	const int tag_col = column_of(r, rubato::column_tag_bpm);
	const int key_col = column_of(r, rubato::column_key);
	check(r.columns()[0] == rubato::column_title && bpm_col == 1, "title, then BPM");
	check(tag_col == 2, "the tag column beside it, since both files had a BPM");
	check(key_col > 0 && column_of(r, rubato::column_tuning) == key_col + 1,
	      "key and tuning, since the keys were measured");
	std::printf("row 0:");
	for (rubato::column_id c : r.columns()) std::printf(" | %s", r.cell(0, c).c_str());
	std::printf("\n");
	check(r.cell(0, rubato::column_title) == "tango.flac", "title falls back to the file name");
	check(r.cell(0, rubato::column_tag_bpm) == get(flac, "BPM"), "tag column as the file had it");
	check(r.cell(1, rubato::column_tag_bpm) == get(mp3, "BEATS_PER_MINUTE"),
	      "tag column reads TBPM in an mp3");
	check(!r.tooltip(0, false).empty(), "a row with a tuning has a tooltip");
	check(r.tooltip(0, true).compare(0, 10, "tango.flac") == 0, "a clipped title leads the tooltip");

	const double measured = std::atof(r.cell(0, rubato::column_bpm).c_str());
	r.scale(0, 2.0);
	check(std::fabs(std::atof(r.cell(0, rubato::column_bpm).c_str()) - 2 * measured) < 0.011,
	      "double in the window doubles the row");
	check(r.cell(0, rubato::column_tag_bpm) == get(flac, "BPM"), "the tag column does not move");

	r.commit();
	r.commit();   // a second click must not write twice
	rubato::wait_until_idle();
	check(flac.writes == flac_writes + 1 && mp3.writes == mp3_writes + 1, "Update writes each once");
	check(std::fabs(number(flac, "BPM") - 2 * measured) < 0.011, "the doubled figure is written");
	check(get(flac, "BpmAlgorithm").empty(), "a row doubled in the window loses BpmAlgorithm");
	check(get(mp3, "BpmAlgorithm").compare(0, 7, "Rubato;") == 0, "an untouched row keeps it");
	recorder::results.clear();

	// --- cancelling a scan puts up no window and writes nothing ---
	recorder::progress_shown = 0;
	analyse->callback2(analyse, DDB_ACTION_CTX_SELECTION);
	if (recorder::last_progress) recorder::last_progress->cancel();
	rubato::wait_until_idle();
	check(recorder::progress_shown == 1 && recorder::results.empty(), "cancelled: no results window");
	check(flac.writes == flac_writes + 1, "cancelled: nothing written");

	// --- auto-write skips the window ---
	conf_ints["rubato.auto_write"] = 1;
	analyse->callback2(analyse, DDB_ACTION_CTX_SELECTION);
	rubato::wait_until_idle();
	check(recorder::results.empty() && flac.writes == flac_writes + 2,
	      "auto-write: written with no results window");
	conf_ints["rubato.auto_write"] = 0;

	// --- tapping ---
	rubato::settings s;
	s.taps_to_average = 4;
	s.seconds_to_reset = 2;
	s.precision = 1;
	rubato::tap_meter meter(s);
	const auto t0 = rubato::tap_meter::clock::now();
	auto at = [t0](double seconds) {
		return t0 + std::chrono::duration_cast<rubato::tap_meter::clock::duration>(
			std::chrono::duration<double>(seconds));
	};
	meter.tap(at(0));
	check(meter.bpm() == 0 && meter.text().empty(), "one tap is no tempo");
	meter.tap(at(0.5));
	meter.tap(at(1.0));
	check(std::fabs(meter.bpm() - 120) < 1e-6 && meter.text() == "120.0", "taps half a second apart: 120");
	meter.tap(at(1.6)); meter.tap(at(2.2)); meter.tap(at(2.8));
	check(meter.taps() == 4 && std::fabs(meter.bpm() - 100) < 1e-6,
	      "only the last four count: 100");
	meter.tap(at(5.0));
	check(meter.taps() == 1, "a pause past the reset time starts again");

	playing = nullptr;
	check(!rubato::write_tapped_bpm(96), "nothing playing: nothing written");
	playing = &mp3;
	const int before_tap = mp3.writes;
	check(rubato::write_tapped_bpm(96.4), "a tap is written to what is playing");
	rubato::wait_until_idle();
	check(mp3.writes == before_tap + 1 && number(mp3, "BEATS_PER_MINUTE") == 96.4,
	      "the tapped BPM, in TBPM");
	check(get(mp3, "BpmAlgorithm").empty() && get(mp3, "INITIALBPM").empty(),
	      "a tap removes BpmAlgorithm and INITIALBPM");
	check(get(mp3, "KEY").empty() || !get(mp3, "KEYCONFIDENCE").empty(), "a tap leaves the key");
	check(rubato::playing_title() == "song.mp3", "the tapping window names what is playing");
	playing = nullptr;

	DB_plugin_action_t * tap = find_action(p, "rubato_tap");
	tap->callback2(tap, DDB_ACTION_CTX_SELECTION);
	check(recorder::tap_shown == 1, "the action opens the tapping window");

	check(p->stop() == 0, "stops");

	bool balanced = true;
	for (fake_track * t : playlist) balanced = balanced && t->refs == 1;
	check(balanced, "every track reference released");
	check(plt_refs == 0, "every playlist reference released");

	std::printf(failures == 0 ? "all passed\n" : "%d failed\n", failures);
	return failures == 0 ? 0 : 1;
}
