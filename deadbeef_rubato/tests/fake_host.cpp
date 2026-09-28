// A stand-in for DeaDBeeF, for the tests. See fake_host.h.

#include "fake_host.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace fake
{

// --- tracks and the playlist -----------------------------------------------


std::recursive_mutex pl_mutex;
std::vector<fake_track *> playlist;
ddb_playlist_t the_playlist;
std::map<std::string, int> conf_ints;
std::map<std::string, std::string> conf_strings;
fake_track * playing = nullptr;
DB_plugin_t * gtkui = nullptr;
bool quiet = false;

fake_track * T(DB_playItem_t * it) { return reinterpret_cast<fake_track *>(it); }

bool same_key(const std::string & a, const char * b)
{
	if (a.size() != std::strlen(b)) return false;
	for (std::size_t i = 0; i < a.size(); i++)
		if (std::tolower(static_cast<unsigned char>(a[i]))
		    != std::tolower(static_cast<unsigned char>(b[i]))) return false;
	return true;
}

const char * find_meta(DB_playItem_t * it, const char * key)
{
	for (auto & m : T(it)->meta) if (same_key(m.first, key)) return m.second.c_str();
	return nullptr;
}

void delete_meta(DB_playItem_t * it, const char * key)
{
	auto & meta = T(it)->meta;
	meta.erase(std::remove_if(meta.begin(), meta.end(),
	                          [key](const std::pair<std::string, std::string> & m)
	                          { return same_key(m.first, key); }), meta.end());
}

void replace_meta(DB_playItem_t * it, const char * key, const char * value)
{
	for (auto & m : T(it)->meta)
		if (same_key(m.first, key)) { m.second = value; return; }
	T(it)->meta.emplace_back(key, value);
}

std::string get(fake_track & t, const char * key)
{
	const char * v = find_meta(&t.base, key);
	return v != nullptr ? v : "";
}

// --- a decoder -------------------------------------------------------------

struct fake_fileinfo
{
	DB_fileinfo_t base;
	fake_track * track;
	std::size_t frame = 0;
	std::size_t frames = 0;
};

DB_decoder_t fake_decoder;

DB_fileinfo_t * dec_open(uint32_t)
{
	fake_fileinfo * info = new fake_fileinfo();
	info->base.plugin = &fake_decoder;
	return &info->base;
}

int dec_init(DB_fileinfo_t * base, DB_playItem_t * it)
{
	fake_fileinfo * info = reinterpret_cast<fake_fileinfo *>(base);
	info->track = T(it);
	base->fmt.channels = 2;
	base->fmt.channelmask = 3;
	base->fmt.samplerate = static_cast<int>(info->track->rate);
	base->fmt.bps = info->track->int16 ? 16 : 32;
	base->fmt.is_float = info->track->int16 ? 0 : 1;
	info->frames = static_cast<std::size_t>(info->track->seconds * info->track->rate);
	return 0;
}

void dec_free(DB_fileinfo_t * base)
{
	delete reinterpret_cast<fake_fileinfo *>(base);
}

//! A band-limited click on every beat with an accent every fourth, as in
//! bpmcore_test's synth_beats, and a held A minor chord underneath so there
//! is a pitch for the key detector to find.
float sample_at(const fake_track & t, std::size_t frame)
{
	const double pi = 3.14159265358979323846;
	const double s = static_cast<double>(frame) / t.rate;
	const double beat = 60.0 / t.click_bpm;
	const double into = std::fmod(s, beat);
	double v = 0;
	if (into < 0.12)
	{
		const double env = 0.5 * (1.0 - std::cos(2.0 * pi * into / 0.12)) * std::exp(-12.0 * into);
		const double partials[4] = { 80.0, 640.0, 2200.0, 4000.0 };
		for (double f : partials) v += std::sin(2.0 * pi * f * s);
		const long index = static_cast<long>(s / beat);
		v *= 0.2 * env * ((index % 4) == 0 ? 1.0 : 0.55);
	}
	const double chord[3] = { 220.0, 261.63, 329.63 };
	for (double f : chord) v += 0.05 * std::sin(2.0 * pi * f * s);
	return static_cast<float>(v);
}

int dec_read(DB_fileinfo_t * base, char * buffer, int nbytes)
{
	fake_fileinfo * info = reinterpret_cast<fake_fileinfo *>(base);
	const int frame_bytes = base->fmt.channels * base->fmt.bps / 8;
	int written = 0;
	while (written + frame_bytes <= nbytes && info->frame < info->frames)
	{
		const float v = sample_at(*info->track, info->frame++);
		for (int c = 0; c < base->fmt.channels; c++)
		{
			if (base->fmt.is_float)
				std::memcpy(buffer + written, &v, sizeof(v));
			else
			{
				const int16_t s = static_cast<int16_t>(std::lround(v * 32767.0));
				std::memcpy(buffer + written, &s, sizeof(s));
			}
			written += base->fmt.bps / 8;
		}
	}
	return written;
}

int dec_write_metadata(DB_playItem_t * it)
{
	std::lock_guard<std::recursive_mutex> guard(pl_mutex);
	T(it)->writes++;
	return 0;
}

// --- DB_functions_t ----------------------------------------------------------

void f_log_detailed(DB_plugin_t *, uint32_t layers, const char * fmt, ...)
{
	if (quiet) return;
	va_list args;
	va_start(args, fmt);
	std::printf("%s", layers == DDB_LOG_LAYER_INFO ? "[info]  " : "[error] ");
	std::vprintf(fmt, args);
	va_end(args);
}

int f_conf_get_int(const char * key, int def)
{
	auto it = conf_ints.find(key);
	return it != conf_ints.end() ? it->second : def;
}

void f_conf_get_str(const char * key, const char * def, char * buffer, int size)
{
	auto it = conf_strings.find(key);
	std::snprintf(buffer, static_cast<std::size_t>(size), "%s",
	              it != conf_strings.end() ? it->second.c_str() : def);
}

void f_pl_lock() { pl_mutex.lock(); }
void f_pl_unlock() { pl_mutex.unlock(); }

void f_pl_item_ref(DB_playItem_t * it)
{
	std::lock_guard<std::recursive_mutex> guard(pl_mutex);
	T(it)->refs++;
}

void f_pl_item_unref(DB_playItem_t * it)
{
	std::lock_guard<std::recursive_mutex> guard(pl_mutex);
	T(it)->refs--;
}

const char * f_pl_find_meta(DB_playItem_t * it, const char * key)
{
	if (std::strcmp(key, ":DECODER") == 0) return "fake";
	return find_meta(it, key);
}

void f_pl_replace_meta(DB_playItem_t * it, const char * key, const char * value)
{
	std::lock_guard<std::recursive_mutex> guard(pl_mutex);
	replace_meta(it, key, value);
}

void f_pl_delete_meta(DB_playItem_t * it, const char * key)
{
	std::lock_guard<std::recursive_mutex> guard(pl_mutex);
	delete_meta(it, key);
}

uint32_t f_pl_get_item_flags(DB_playItem_t * it) { return T(it)->subtrack ? DDB_IS_SUBTRACK : 0; }
float f_pl_get_item_duration(DB_playItem_t * it) { return static_cast<float>(T(it)->seconds); }
int f_pl_is_selected(DB_playItem_t * it) { return T(it)->selected ? 1 : 0; }

DB_plugin_t * f_plug_get_for_id(const char * id)
{
	if (std::strcmp(id, "fake") == 0) return &fake_decoder.plugin;
	if (gtkui != nullptr && gtkui->id != nullptr && std::strcmp(id, gtkui->id) == 0) return gtkui;
	return nullptr;
}

int plt_refs = 0;
int modified = 0;
int saves = 0;

ddb_playlist_t * f_action_get_playlist() { plt_refs++; return &the_playlist; }
ddb_playlist_t * f_pl_get_playlist(DB_playItem_t *) { plt_refs++; return &the_playlist; }
void f_plt_unref(ddb_playlist_t *) { plt_refs--; }
void f_plt_modified(ddb_playlist_t *) { modified++; }
int f_pl_save_all() { saves++; return 0; }

DB_playItem_t * at(std::size_t i)
{
	if (i >= playlist.size()) return nullptr;
	playlist[i]->refs++;
	return &playlist[i]->base;
}

DB_playItem_t * f_plt_get_first(ddb_playlist_t *, int) { return at(0); }

DB_playItem_t * f_pl_get_next(DB_playItem_t * it, int)
{
	for (std::size_t i = 0; i < playlist.size(); i++)
		if (&playlist[i]->base == it) return at(i + 1);
	return nullptr;
}

int f_pcm_convert(const ddb_waveformat_t * in, const char * input, const ddb_waveformat_t * out,
                  char * output, int inputsize)
{
	if (in->bps != 16 || in->is_float || out->bps != 32 || !out->is_float) std::abort();
	const int n = inputsize / 2;
	for (int i = 0; i < n; i++)
	{
		int16_t s;
		std::memcpy(&s, input + 2 * i, 2);
		const float f = s / 32768.0f;
		std::memcpy(output + 4 * i, &f, 4);
	}
	return n * 4;
}

int events = 0;

ddb_event_t * f_event_alloc(uint32_t id)
{
	ddb_event_track_t * ev = static_cast<ddb_event_track_t *>(std::calloc(1, sizeof(ddb_event_track_t)));
	ev->ev.event = static_cast<int>(id);
	ev->ev.size = sizeof(ddb_event_track_t);
	return &ev->ev;
}

int f_event_send(ddb_event_t * ev, uint32_t, uint32_t)
{
	ddb_event_track_t * tev = reinterpret_cast<ddb_event_track_t *>(ev);
	if (ev->event == DB_EV_TRACKINFOCHANGED && tev->track != nullptr)
	{
		events++;
		f_pl_item_unref(tev->track);
	}
	std::free(ev);
	return 0;
}

DB_playItem_t * f_streamer_get_playing_track()
{
	if (playing == nullptr) return nullptr;
	f_pl_item_ref(&playing->base);
	return &playing->base;
}

DB_functions_t make_api()
{
	DB_functions_t api;
	std::memset(&api, 0, sizeof(api));
	api.log_detailed = f_log_detailed;
	api.conf_get_int = f_conf_get_int;
	api.conf_get_str = f_conf_get_str;
	api.pl_lock = f_pl_lock;
	api.pl_unlock = f_pl_unlock;
	api.pl_item_ref = f_pl_item_ref;
	api.pl_item_unref = f_pl_item_unref;
	api.pl_find_meta = f_pl_find_meta;
	api.pl_find_meta_raw = f_pl_find_meta;
	api.pl_replace_meta = f_pl_replace_meta;
	api.pl_delete_meta = f_pl_delete_meta;
	api.pl_get_item_flags = f_pl_get_item_flags;
	api.pl_get_item_duration = f_pl_get_item_duration;
	api.pl_is_selected = f_pl_is_selected;
	api.plug_get_for_id = f_plug_get_for_id;
	api.action_get_playlist = f_action_get_playlist;
	api.pl_get_playlist = f_pl_get_playlist;
	api.plt_unref = f_plt_unref;
	api.plt_modified = f_plt_modified;
	api.pl_save_all = f_pl_save_all;
	api.plt_get_first = f_plt_get_first;
	api.pl_get_next = f_pl_get_next;
	api.pcm_convert = f_pcm_convert;
	api.streamer_get_playing_track = f_streamer_get_playing_track;
	api.event_alloc = f_event_alloc;
	api.event_send = f_event_send;
	return api;
}

DB_plugin_action_t * find_action(DB_plugin_t * p, const char * name)
{
	for (DB_plugin_action_t * a = p->get_actions(nullptr); a != nullptr; a = a->next)
		if (std::strcmp(a->name, name) == 0) return a;
	return nullptr;
}

double number(fake_track & t, const char * key)
{
	const std::string v = get(t, key);
	return v.empty() ? 0 : std::atof(v.c_str());
}

//! Within 2% of the click rate at the beat, the half bar or the bar - which
//! level bpmcore reports a bare click track at is its business, not this
//! test's.
bool near_level(double bpm, double click)
{
	for (double level : { click, click / 2, click / 4, click * 2 })
		if (std::fabs(bpm - level) <= level * 0.02) return true;
	return false;
}


void init()
{
	fake_decoder.plugin.type = DB_PLUGIN_DECODER;
	fake_decoder.plugin.id = "fake";
	fake_decoder.open = dec_open;
	fake_decoder.init = dec_init;
	fake_decoder.free = dec_free;
	fake_decoder.read = dec_read;
	fake_decoder.write_metadata = dec_write_metadata;
}

}   // namespace fake
