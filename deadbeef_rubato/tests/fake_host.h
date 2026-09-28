#ifndef RUBATO_FAKE_HOST_H
#define RUBATO_FAKE_HOST_H

// A stand-in for DeaDBeeF: the part of DB_functions_t the plugin calls - a
// playlist, track metadata, a decoder that synthesises a click track, the
// configuration, the log and what is playing - so the real plugin can be run
// with no player installed. Shared by ddb_rubato_hosttest and
// rubato_gtk_preview.
//
// What it cannot stand in for is the player's own half: whether a decoder's
// write_metadata puts a field where the plugin expects, which rubato_format.h
// documents and the README describes.

#include <cstddef>
#include <map>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include "rubato_plugin.h"

namespace fake
{

struct fake_track
{
	DB_playItem_t base;   // first, so a DB_playItem_t * is a fake_track *
	std::vector<std::pair<std::string, std::string>> meta;
	bool selected = true;
	bool subtrack = false;
	int refs = 1;         // the playlist's own
	int writes = 0;

	// What the fake decoder produces for it.
	unsigned rate = 44100;
	bool int16 = false;
	double seconds = 60;
	double click_bpm = 120;
};

extern std::recursive_mutex pl_mutex;
extern std::vector<fake_track *> playlist;
extern std::map<std::string, int> conf_ints;
extern std::map<std::string, std::string> conf_strings;
extern fake_track * playing;   //!< what streamer_get_playing_track returns
//! Returned for its own id by plug_get_for_id, to stand in for DeaDBeeF's
//! interface plugin; null for none.
extern DB_plugin_t * gtkui;
extern bool quiet;             //!< no log on stdout

extern int plt_refs;
extern int modified;
extern int saves;
extern int events;

//! Sets up the decoder; call once first.
void init();
DB_functions_t make_api();

//! A field, read without the plugin's help; blank for none.
std::string get(fake_track & t, const char * key);
double number(fake_track & t, const char * key);
DB_plugin_action_t * find_action(DB_plugin_t * p, const char * name);
//! Within 2% of `click` at the beat, the half bar, the bar or twice the beat.
bool near_level(double bpm, double click);

}   // namespace fake

#endif // RUBATO_FAKE_HOST_H
