#ifndef RUBATO_PREVIEW_TRACKS_H
#define RUBATO_PREVIEW_TRACKS_H

// The made-up playlist rubato_gtk_preview and rubato_cocoa_preview scan, so
// the two toolkits' windows are shown the same rows: a tag BPM on some and
// not others, a doubled tap, a title too long for its column, and a track
// with no year.

#include <string>
#include <vector>

#include "fake_host.h"

namespace fake
{

inline void add_preview_tracks()
{
	struct made_up { const char * title; const char * path; double bpm; const char * tag; const char * year; };
	static const made_up tracks[] = {
		{ "La Cumparsita", "/m/01.flac", 118, "118", "1937" },
		{ "Poema", "/m/02.flac", 62, "124", "1935" },
		{ "Milonga Sentimental", "/m/03.mp3", 96, nullptr, "1941" },
		{ "Desde el alma", "/m/04.flac", 64, "66", "1948" },
		{ "Una noche de garufa (instrumental, remastered from the original shellac)", "/m/05.flac", 122, nullptr, "1941" },
		{ "Recuerdo", "/m/06.m4a", 110, "110.24", "1944" },
		{ "Café Domínguez", "/m/07.flac", 116, nullptr, nullptr },
		{ "La Yumba", "/m/08.flac", 120, "120", "1946" },
	};
	static std::vector<fake_track> store(sizeof(tracks) / sizeof(tracks[0]));
	for (std::size_t i = 0; i < store.size(); i++)
	{
		fake_track & t = store[i];
		t.meta.emplace_back(":URI", tracks[i].path);
		t.meta.emplace_back("title", tracks[i].title);
		if (tracks[i].tag != nullptr)
			t.meta.emplace_back(std::string(tracks[i].path).find(".mp3") != std::string::npos
			                    ? "BEATS_PER_MINUTE" : "BPM", tracks[i].tag);
		if (tracks[i].year != nullptr) t.meta.emplace_back("year", tracks[i].year);
		t.click_bpm = tracks[i].bpm;
		t.seconds = 600;   // long enough for the progress window to appear
		playlist.push_back(&t);
	}
	playing = &store[0];
	conf_ints["rubato.bpm_precision"] = 1;
}

}   // namespace fake

#endif // RUBATO_PREVIEW_TRACKS_H
