// Rubato BPM Analyzer for DeaDBeeF.
//
// The same analysis as foo_rubato - bpmcore, untouched - behind DeaDBeeF's
// plugin API instead of foobar2000's. What it does with a track is what
// foo_rubato does with "Write tags automatically" on: decode it once, measure
// the tempo, the rhythm, the tuning and the key, and write the same fields
// under the same rules. See rubato_format.h for where the field names differ.
//
// What it leaves out is the windowing. DeaDBeeF has no toolkit-neutral way to
// put up a window - GTK on Linux and Windows, Cocoa on macOS, each its own
// plugin - so there is no results window and no tapping window. The log
// window takes the place of the first: every track gets one line there, and
// "Analyse without writing tags" is the way to look before committing.

#include "rubato_plugin.h"
#include "rubato_format.h"
#include "rubato_version.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <bpmcore/bpmcore.h>
// Which transform is linked in, and so which licence goes in the about text.
// Host-free, so shared with foo_rubato rather than copied.
#include <foo_rubato/fft_license.h>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#elif defined(__APPLE__)
#include <pthread.h>
#include <sys/qos.h>
#elif defined(__linux__)
#include <sys/resource.h>
#include <sys/syscall.h>
#include <unistd.h>
#endif

namespace
{

DB_functions_t * deadbeef = nullptr;
DB_misc_t plugin;

// --- fields ----------------------------------------------------------------
//
// The same names foo_rubato writes, so a library tagged by one reads the same
// in the other. The BPM and the key's standard slot vary by container; see
// rubato::bpm_field and rubato::initial_key_field.

const char * const tag_bpm_algorithm    = "BpmAlgorithm";
const char * const tag_key_algorithm    = "KeyAlgorithm";
const char * const tag_tuning_algorithm = "TuningAlgorithm";
const char * const tag_initial_bpm      = "INITIALBPM";
const char * const tag_genre            = "genre";
const char * const tag_key              = "KEY";
const char * const tag_key_candidates   = "KEYCANDIDATES";
const char * const tag_key_confidence   = "KEYCONFIDENCE";
const char * const tag_mode_balance     = "MODEBALANCE";
const char * const tag_tuning           = "TUNING";
const char * const tag_retune           = "RETUNE";
const char * const tag_retune_candidates = "RETUNECANDIDATES";

// --- logging ---------------------------------------------------------------
//
// One line per track at the info layer: with no results window, that line is
// the result. Failures go to the default layer, which DeaDBeeF's interfaces
// answer by opening the log window. The detail foo_rubato prints under
// "Diagnostics" is at the info layer too, behind a setting of its own - the
// default layer cannot be used for it, because it is shown whenever the
// plugin logs at all.

void log_at(uint32_t layer, const char * fmt, ...)
{
	char text[2048];
	va_list args;
	va_start(args, fmt);
	std::vsnprintf(text, sizeof(text), fmt, args);
	va_end(args);
	deadbeef->log_detailed(&plugin.plugin, layer, "rubato: %s\n", text);
}

#define RUBATO_INFO(...)  log_at(DDB_LOG_LAYER_INFO, __VA_ARGS__)
#define RUBATO_ERROR(...) log_at(DDB_LOG_LAYER_DEFAULT, __VA_ARGS__)

// --- settings --------------------------------------------------------------

struct settings
{
	int precision = rubato::bpm_precision_whole;
	std::string bpm_tag = "BPM";
	bool write_initial = true;
	bool write_algorithm = true;
	bool detect_key = true;
	bool write_key = true;
	bool write_tuning = true;
	bool write_retune = true;
	bool skip_tagged = false;
	bool diagnostics = false;
};

//! Read once when an action starts rather than per track, so a setting
//! changed while a selection is being written cannot apply to half of it.
settings read_settings()
{
	settings s;
	s.precision = deadbeef->conf_get_int("rubato.bpm_precision", 0);
	char tag[128];
	deadbeef->conf_get_str("rubato.bpm_tag", "BPM", tag, sizeof(tag));
	// An empty name would write the BPM nowhere and remove nothing.
	if (tag[0] != '\0') s.bpm_tag = tag;
	s.write_initial   = deadbeef->conf_get_int("rubato.write_initial_bpm", 1) != 0;
	s.write_algorithm = deadbeef->conf_get_int("rubato.write_algorithm", 1) != 0;
	s.detect_key      = deadbeef->conf_get_int("rubato.detect_key", 1) != 0;
	s.write_key       = deadbeef->conf_get_int("rubato.write_key", 1) != 0;
	s.write_tuning    = deadbeef->conf_get_int("rubato.write_tuning", 1) != 0;
	s.write_retune    = deadbeef->conf_get_int("rubato.write_retune", 1) != 0;
	s.skip_tagged     = deadbeef->conf_get_int("rubato.skip_tagged", 0) != 0;
	s.diagnostics     = deadbeef->conf_get_int("rubato.diagnostics", 0) != 0;
	return s;
}

const char settings_dialog[] =
	"property \"BPM precision\" select[3] rubato.bpm_precision 0 "
		"\"Whole number\" \"One decimal\" \"Two decimals\";\n"
	"property \"BPM field name\" entry rubato.bpm_tag \"BPM\";\n"
	"property \"Skip tracks that already have a BPM\" checkbox rubato.skip_tagged 0;\n"
	"property \"Write INITIALBPM (the tempo the track opens at)\" checkbox rubato.write_initial_bpm 1;\n"
	"property \"Write BpmAlgorithm and KeyAlgorithm\" checkbox rubato.write_algorithm 1;\n"
	"property \"Detect tuning and key\" checkbox rubato.detect_key 1;\n"
	"property \"Write KEY, KEYCANDIDATES and KEYCONFIDENCE\" checkbox rubato.write_key 1;\n"
	"property \"Write TUNING\" checkbox rubato.write_tuning 1;\n"
	"property \"Write RETUNE\" checkbox rubato.write_retune 1;\n"
	"property \"Log the details of each analysis\" checkbox rubato.diagnostics 0;\n";

// --- track metadata --------------------------------------------------------

//! A copy of one field, or an empty string. Copied under the lock because the
//! pointer DeaDBeeF hands back is only good while it is held.
std::string meta(DB_playItem_t * track, const char * key)
{
	deadbeef->pl_lock();
	const char * value = deadbeef->pl_find_meta(track, key);
	std::string out = value != nullptr ? value : "";
	deadbeef->pl_unlock();
	return out;
}

std::string file_name(const std::string & path)
{
	const std::size_t slash = path.find_last_of("/\\");
	return slash == std::string::npos ? path : path.substr(slash + 1);
}

int year_of(DB_playItem_t * track)
{
	for (const char * field : rubato::year_fields)
	{
		const int year = rubato::year_from_tag(meta(track, field).c_str());
		if (year != 0) return year;
	}
	return 0;
}

//! Writes `value` to `field`, or removes the field when `value` is empty.
//! `enabled` governs writing only: switching a field off means this plugin
//! stops adding it, not that it leaves behind a value it knows is stale.
//! Called with the playlist lock held.
void set_or_remove(DB_playItem_t * track, const char * field, const std::string & value,
                   bool enabled)
{
	if (value.empty() || !enabled) deadbeef->pl_delete_meta(track, field);
	else deadbeef->pl_replace_meta(track, field, value.c_str());
}

// --- writing ---------------------------------------------------------------

//! One writer at a time. The decoders' tag writers were written for the track
//! properties dialog, which writes from one thread, and nothing promises they
//! are safe side by side; writing is a small part of a scan either way.
std::mutex write_lock;

//! The decoder that can write this track's tags, or null with the reason
//! already logged.
DB_decoder_t * writer_for(DB_playItem_t * track, const std::string & name)
{
	deadbeef->pl_lock();
	const bool subtrack = (deadbeef->pl_get_item_flags(track) & DDB_IS_SUBTRACK) != 0;
	const char * id = deadbeef->pl_find_meta_raw(track, ":DECODER");
	const std::string decoder_id = id != nullptr ? id : "";
	deadbeef->pl_unlock();

	if (subtrack)
	{
		RUBATO_INFO("%s is one track of a multi-track file (a cue sheet, most often), "
		            "which DeaDBeeF cannot write tags to; nothing written.", name.c_str());
		return nullptr;
	}
	DB_decoder_t * dec = decoder_id.empty() ? nullptr
		: reinterpret_cast<DB_decoder_t *>(deadbeef->plug_get_for_id(decoder_id.c_str()));
	if (dec == nullptr || dec->write_metadata == nullptr)
	{
		RUBATO_INFO("DeaDBeeF cannot write tags to %s (decoder \"%s\"); nothing written.",
		            name.c_str(), decoder_id.c_str());
		return nullptr;
	}
	return dec;
}

//! Hands the changed fields to the decoder, and tells the rest of the player
//! the track changed. Call without the playlist lock.
bool commit(DB_decoder_t * dec, DB_playItem_t * track, const std::string & name)
{
	int failed;
	{
		std::lock_guard<std::mutex> guard(write_lock);
		failed = dec->write_metadata(track);
	}
	if (failed)
	{
		RUBATO_ERROR("could not write tags to %s.", name.c_str());
		return false;
	}

	ddb_event_track_t * ev =
		reinterpret_cast<ddb_event_track_t *>(deadbeef->event_alloc(DB_EV_TRACKINFOCHANGED));
	ev->track = track;
	deadbeef->pl_item_ref(track);   // the event releases it
	deadbeef->event_send(reinterpret_cast<ddb_event_t *>(ev), 0, 0);

	if (ddb_playlist_t * plt = deadbeef->pl_get_playlist(track))
	{
		deadbeef->plt_modified(plt);
		deadbeef->plt_unref(plt);
	}
	return true;
}

//! What one track's analysis came back with, as it is written out.
struct track_result
{
	bpmcore::analysis analysis;
	int year = 0;
};

//! Every field an analysis writes, with the same rules as foo_rubato's
//! file_info_filter_bpm. Called with the playlist lock held.
void apply_analysis(DB_playItem_t * track, const std::string & path, const track_result & r,
                    const settings & s)
{
	const bpmcore::analysis & a = r.analysis;
	const rubato::container c = rubato::container_of(path.c_str());

	// A tempo the grid never settled on leaves the BPM fields as they were:
	// there is nothing measured to put there, and the key and tuning below
	// stand on their own.
	if (a.ok)
	{
		const char * bpm_field = rubato::bpm_field(c, s.bpm_tag.c_str());
		const int precision = rubato::bpm_precision_for(c, s.precision);
		deadbeef->pl_replace_meta(track, bpm_field,
		                          rubato::format_bpm(a.bpm, precision).c_str());
		// In an mp3 the default name moved onto TBPM; a TXXX:BPM left beside
		// it from another tagger would be a second, stale answer.
		if (std::strcmp(bpm_field, s.bpm_tag.c_str()) != 0)
			deadbeef->pl_delete_meta(track, s.bpm_tag.c_str());

		if (s.write_algorithm) deadbeef->pl_replace_meta(track, tag_bpm_algorithm, RUBATO_ALGORITHM);
		else deadbeef->pl_delete_meta(track, tag_bpm_algorithm);

		std::string initial;
		if (a.initial_bpm > 0) initial = rubato::format_bpm(a.initial_bpm, precision);
		set_or_remove(track, tag_initial_bpm, initial, s.write_initial);

		const char * existing = deadbeef->pl_find_meta(track, tag_genre);
		if (const char * genre = rubato::genre_to_write(existing, bpmcore::rhythm_name(a.rhythm),
		                                                a.confidence))
			deadbeef->pl_replace_meta(track, tag_genre, genre);
	}

	// The key, and always beside it how far to trust it: KEY alone reads as
	// a fact, and it is right 79% of the time.
	const std::string key_name = rubato::format_key(a.key);
	set_or_remove(track, tag_key, key_name, s.write_key);
	set_or_remove(track, tag_key_candidates, rubato::format_key_candidates(a.key), s.write_key);
	set_or_remove(track, tag_key_confidence, rubato::format_key_confidence(a.key), s.write_key);
	set_or_remove(track, tag_mode_balance, rubato::format_mode_balance(a.key), s.write_key);

	// The key again in the container's standard slot. That slot is shared
	// with other taggers, so it is cleared only where our attribution says we
	// were the ones who filled it.
	const bool key_ours = s.write_key && !key_name.empty();
	const char * const key_slot = rubato::initial_key_field(c);
	const rubato::attribution key_was = rubato::attribution_of(
		deadbeef->pl_find_meta(track, tag_key_algorithm), RUBATO_ALGORITHM_NAME);
	if (key_ours)
	{
		deadbeef->pl_replace_meta(track, key_slot, key_name.c_str());
		if (s.write_algorithm) deadbeef->pl_replace_meta(track, tag_key_algorithm, RUBATO_ALGORITHM);
		else deadbeef->pl_delete_meta(track, tag_key_algorithm);
	}
	else if (key_was == rubato::attribution_ours)
	{
		deadbeef->pl_delete_meta(track, key_slot);
		deadbeef->pl_delete_meta(track, tag_key_algorithm);
	}

	// Tuning, on the same terms: TUNING is beaTunes' field too.
	const std::string tuning = rubato::format_tuning(a.key);
	const rubato::attribution tuning_was = rubato::attribution_of(
		deadbeef->pl_find_meta(track, tag_tuning_algorithm), RUBATO_ALGORITHM_NAME);
	if (s.write_tuning && !tuning.empty())
	{
		deadbeef->pl_replace_meta(track, tag_tuning, tuning.c_str());
		if (s.write_algorithm) deadbeef->pl_replace_meta(track, tag_tuning_algorithm, RUBATO_ALGORITHM);
		else deadbeef->pl_delete_meta(track, tag_tuning_algorithm);
	}
	else if (tuning_was != rubato::attribution_foreign)
	{
		deadbeef->pl_delete_meta(track, tag_tuning);
		deadbeef->pl_delete_meta(track, tag_tuning_algorithm);
	}

	set_or_remove(track, tag_retune, rubato::format_retune(a.key, r.year), s.write_retune);
	set_or_remove(track, tag_retune_candidates,
	              rubato::format_retune_candidates(a.key, r.year), s.write_retune);
}

// --- decoding --------------------------------------------------------------

std::atomic<bool> aborting(false);

class core_listener : public bpmcore::listener
{
public:
	bool cancelled() override { return aborting.load(); }
};

//! Lower the calling thread's priority, so a library scan started in the
//! middle of a set cannot take the core playback is decoding on.
void deprioritise_this_thread()
{
#if defined(_WIN32)
	SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
#elif defined(__APPLE__)
	pthread_set_qos_class_self_np(QOS_CLASS_UTILITY, 0);
#elif defined(__linux__)
	// Linux schedules threads as processes, so a thread's nice value is its
	// own. Raising it needs no privilege.
	setpriority(PRIO_PROCESS, static_cast<id_t>(syscall(SYS_gettid)), 10);
#endif
}

//! Decode the whole track through its own decoder, and analyse it.
//! `ok` says whether any audio arrived; the analysis says the rest.
bpmcore::analysis decode_and_analyse(DB_playItem_t * track, const std::string & name,
                                     const settings & s, int analysis_threads, bool & ok)
{
	ok = false;
	bpmcore::analysis result;

	deadbeef->pl_lock();
	const char * id = deadbeef->pl_find_meta(track, ":DECODER");
	const std::string decoder_id = id != nullptr ? id : "";
	deadbeef->pl_unlock();

	DB_decoder_t * dec = decoder_id.empty() ? nullptr
		: reinterpret_cast<DB_decoder_t *>(deadbeef->plug_get_for_id(decoder_id.c_str()));
	if (dec == nullptr)
	{
		RUBATO_ERROR("no decoder for %s.", name.c_str());
		return result;
	}

	const auto started = std::chrono::steady_clock::now();

	// The signal as it is in the file: no ReplayGain, no DSP. The analysis
	// normalises the level itself, and a limiter in the chain would reshape
	// the very onsets it measures.
	DB_fileinfo_t * info = dec->open(DDB_DECODER_HINT_RAW_SIGNAL);
	if (info == nullptr || dec->init(info, track) != 0)
	{
		RUBATO_ERROR("could not open %s for analysis.", name.c_str());
		if (info != nullptr) dec->free(info);
		return result;
	}

	const ddb_waveformat_t in_fmt = info->fmt;
	const int channels = in_fmt.channels;
	const int frame_bytes = channels * (in_fmt.bps / 8);
	if (channels <= 0 || frame_bytes <= 0 || in_fmt.samplerate <= 0)
	{
		RUBATO_ERROR("%s reports no audio format.", name.c_str());
		dec->free(info);
		return result;
	}

	// DeaDBeeF's decoders hand over whatever the file holds; pcm_convert
	// turns anything that is not already float into float.
	ddb_waveformat_t float_fmt = in_fmt;
	float_fmt.bps = 32;
	float_fmt.is_float = 1;

	const int block_frames = 4096;
	std::vector<char> raw(static_cast<std::size_t>(block_frames) * frame_bytes);
	std::vector<float> samples(static_cast<std::size_t>(block_frames) * channels);

	bpmcore::collector collector(static_cast<unsigned>(in_fmt.samplerate),
	                             deadbeef->pl_get_item_duration(track));
	for (;;)
	{
		if (aborting.load()) break;
		const int bytes = dec->read(info, raw.data(), static_cast<int>(raw.size()));
		if (bytes <= 0) break;
		const int frames = bytes / frame_bytes;

		if (in_fmt.is_float && in_fmt.bps == 32)
			std::memcpy(samples.data(), raw.data(), static_cast<std::size_t>(frames) * frame_bytes);
		else
			deadbeef->pcm_convert(&in_fmt, raw.data(), &float_fmt,
			                      reinterpret_cast<char *>(samples.data()), frames * frame_bytes);

		collector.add_interleaved(samples.data(), static_cast<std::size_t>(frames),
		                          static_cast<unsigned>(channels));
		if (collector.full() || bytes < static_cast<int>(raw.size())) break;
	}
	dec->free(info);

	if (aborting.load()) return result;
	if (collector.size() == 0)
	{
		RUBATO_ERROR("no audio decoded from %s.", name.c_str());
		return result;
	}
	ok = true;

	const auto decoded = std::chrono::steady_clock::now();
	core_listener listener;
	bpmcore::options options;
	options.threads = analysis_threads;
	options.detect_key = s.detect_key;
	result = collector.finish(&listener, &options);
	const auto analysed = std::chrono::steady_clock::now();

	if (s.diagnostics && result.ok)
		RUBATO_INFO("%s -> %.2f BPM, %s (p=%.2f), beat %.2f BPM, %d/4 grid, opens at %.2f BPM, "
		             "fluctuation +/-%.2f BPM over %d windows; %.1fs of audio, %.2fs to read, "
		             "%.2fs to analyse",
		             name.c_str(), result.bpm, bpmcore::rhythm_name(result.rhythm),
		             result.confidence, result.beat_bpm, result.meter, result.initial_bpm,
		             result.bpm_spread, result.spread_windows, result.duration,
		             std::chrono::duration<double>(decoded - started).count(),
		             std::chrono::duration<double>(analysed - decoded).count());
	if (s.diagnostics && result.key.ok)
	{
		const bpmcore::key_analysis & k = result.key;
		RUBATO_INFO("%s -> key %s (%s, margin %.3f), tuning %.1f cents (R=%.2f)%s",
		             name.c_str(), bpmcore::key_name(k.best.root, k.best.minor),
		             bpmcore::key_confidence_name(k.confidence), k.margin, k.tuning_cents,
		             k.tuning_r, k.near_wrap ? ", near the semitone wrap" : "");
	}
	return result;
}

//! The line the log window shows for one analysed track.
std::string summary(const track_result & r)
{
	const bpmcore::analysis & a = r.analysis;
	std::string out;
	if (a.ok)
	{
		out += rubato::format_fixed(a.bpm, 2) + " BPM, ";
		out += bpmcore::rhythm_name(a.rhythm);
		out += " (p=" + rubato::format_fixed(a.confidence, 2) + ")";
		if (a.initial_bpm > 0) out += ", opens at " + rubato::format_fixed(a.initial_bpm, 1);
		if (a.bpm_spread > 0) out += ", +/-" + rubato::format_fixed(a.bpm_spread, 1);
	}
	else
	{
		out += "no tempo measured";
	}
	const std::string key = rubato::format_key(a.key);
	if (!key.empty())
		out += "; key " + key + " (" + bpmcore::key_confidence_name(a.key.confidence) + ")";
	const std::string tuning = rubato::format_tuning(a.key);
	if (!tuning.empty()) out += ", tuning " + tuning + " c";
	const std::string retune = rubato::format_retune(a.key, r.year);
	if (!retune.empty()) out += ", retune " + retune;
	return out;
}

// --- the work queue --------------------------------------------------------

enum job_kind
{
	job_analyse_and_write,
	job_analyse_only,
	job_scale
};

//! One action's worth of tracks: what it was asked for, and how it went.
struct batch
{
	job_kind kind = job_analyse_and_write;
	double scale = 1.0;   //!< job_scale only
	settings s;
	std::size_t total = 0;
	std::atomic<std::size_t> done{0};
	std::atomic<std::size_t> written{0};
	std::atomic<std::size_t> failed{0};
	std::chrono::steady_clock::time_point started = std::chrono::steady_clock::now();
};

struct job
{
	DB_playItem_t * track;   //!< holds a reference
	std::shared_ptr<batch> owner;
};

std::mutex queue_lock;
std::condition_variable queue_changed;
std::deque<job> queue;
std::vector<std::thread> pool;
int busy = 0;
bool stopping = false;

//! Tracks scanned at once: two short of what the machine reports, as in
//! foo_rubato. Decoding dominates a scan and is one core per track, so this
//! is what makes a library go faster; the two left over are for playback and
//! the interface.
int scan_workers()
{
	const int cores = static_cast<int>(std::thread::hardware_concurrency());
	return cores <= 2 ? 1 : cores - 2;
}

void finish_job(const job & j)
{
	batch & b = *j.owner;
	deadbeef->pl_item_unref(j.track);
	if (b.done.fetch_add(1) + 1 != b.total) return;

	const double seconds = std::chrono::duration<double>(
		std::chrono::steady_clock::now() - b.started).count();
	if (b.kind == job_analyse_only)
		RUBATO_INFO("analysed %zu track%s in %.1fs; nothing written.", b.total,
		            b.total == 1 ? "" : "s", seconds);
	else
		RUBATO_INFO("%zu of %zu track%s written in %.1fs%s.", b.written.load(), b.total,
		            b.total == 1 ? "" : "s", seconds,
		            b.failed.load() > 0 ? "; see above for the rest" : "");
	// The playlists hold a copy of every field; save them so the new values
	// are there after a restart without the files being read again.
	if (b.written.load() > 0) deadbeef->pl_save_all();
}

void run_scale(const job & j, const std::string & path, const std::string & name)
{
	batch & b = *j.owner;
	const rubato::container c = rubato::container_of(path.c_str());
	const char * bpm_field = rubato::bpm_field(c, b.s.bpm_tag.c_str());
	const int precision = rubato::bpm_precision_for(c, b.s.precision);

	DB_decoder_t * dec = writer_for(j.track, name);
	if (dec == nullptr) { b.failed++; return; }

	bool changed = false;
	deadbeef->pl_lock();
	double bpm = 0;
	if (rubato::parse_decimal(deadbeef->pl_find_meta(j.track, bpm_field), bpm) && bpm > 0)
	{
		deadbeef->pl_replace_meta(j.track, bpm_field,
		                          rubato::format_bpm(bpm * b.scale, precision).c_str());
		// The user overruling the measurement: the analysis no longer stands
		// behind the number. KeyAlgorithm stays - a corrected metrical level
		// says nothing about the key.
		deadbeef->pl_delete_meta(j.track, tag_bpm_algorithm);
		// The opening tempo was read at the same wrong level, so the same
		// factor puts it right.
		double initial = 0;
		if (rubato::parse_decimal(deadbeef->pl_find_meta(j.track, tag_initial_bpm), initial)
		    && initial > 0)
			deadbeef->pl_replace_meta(j.track, tag_initial_bpm,
			                          rubato::format_bpm(initial * b.scale, precision).c_str());
		changed = true;
	}
	deadbeef->pl_unlock();

	if (!changed)
	{
		RUBATO_INFO("%s has no %s to scale.", name.c_str(), bpm_field);
		b.failed++;
		return;
	}
	if (commit(dec, j.track, name)) b.written++;
	else b.failed++;
}

void run_analysis(const job & j, const std::string & path, const std::string & name,
                  int analysis_threads)
{
	batch & b = *j.owner;
	const rubato::container c = rubato::container_of(path.c_str());
	const std::string had = meta(j.track, rubato::bpm_field(c, b.s.bpm_tag.c_str()));

	if (b.kind == job_analyse_and_write && b.s.skip_tagged && !had.empty())
	{
		RUBATO_INFO("%s already has a BPM (%s); skipped.", name.c_str(), had.c_str());
		return;
	}

	// Checked before the decode rather than after, so a format DeaDBeeF
	// cannot write is not decoded for nothing.
	DB_decoder_t * writer = nullptr;
	if (b.kind == job_analyse_and_write)
	{
		writer = writer_for(j.track, name);
		if (writer == nullptr) { b.failed++; return; }
	}

	track_result r;
	bool decoded = false;
	r.analysis = decode_and_analyse(j.track, name, b.s, analysis_threads, decoded);
	if (aborting.load()) return;
	if (!decoded) { b.failed++; return; }
	r.year = year_of(j.track);

	std::string line = summary(r);
	if (b.kind == job_analyse_only && !had.empty()) line += "; tag had " + had;
	RUBATO_INFO("[%zu/%zu] %s: %s", b.done.load() + 1, b.total, name.c_str(), line.c_str());

	if (b.kind != job_analyse_and_write) return;
	if (!r.analysis.ok && !r.analysis.key.ok)
	{
		b.failed++;
		return;
	}

	deadbeef->pl_lock();
	apply_analysis(j.track, path, r, b.s);
	deadbeef->pl_unlock();
	if (commit(writer, j.track, name)) b.written++;
	else b.failed++;
}

void worker()
{
	deprioritise_this_thread();
	for (;;)
	{
		job j;
		int analysis_threads;
		{
			std::unique_lock<std::mutex> guard(queue_lock);
			queue_changed.wait(guard, [] { return stopping || !queue.empty(); });
			if (stopping) return;
			j = queue.front();
			queue.pop_front();
			busy++;
			// A track on its own gets the machine for its spectral stage;
			// several at once keep one core each, rather than every one of
			// them spreading over the same cores. The answer is the same.
			analysis_threads = (queue.empty() && busy == 1) ? 0 : 1;
		}

		const std::string path = meta(j.track, ":URI");
		const std::string name = file_name(path);
		try
		{
			if (j.owner->kind == job_scale) run_scale(j, path, name);
			else run_analysis(j, path, name, analysis_threads);
		}
		catch (const std::exception & e)
		{
			RUBATO_ERROR("error analysing %s: %s", name.c_str(), e.what());
			j.owner->failed++;
		}
		finish_job(j);

		{
			std::lock_guard<std::mutex> guard(queue_lock);
			busy--;
		}
		queue_changed.notify_all();
	}
}

void enqueue(std::vector<job> & jobs)
{
	{
		std::unique_lock<std::mutex> guard(queue_lock);
		if (stopping)
		{
			guard.unlock();
			for (job & j : jobs) deadbeef->pl_item_unref(j.track);
			return;
		}
		if (pool.empty())
		{
			const int n = scan_workers();
			for (int i = 0; i < n; i++) pool.emplace_back(worker);
		}
		for (job & j : jobs) queue.push_back(j);
	}
	queue_changed.notify_all();
}

// --- actions ---------------------------------------------------------------

//! The tracks an action applies to, each with a reference held.
std::vector<DB_playItem_t *> action_tracks(int ctx)
{
	std::vector<DB_playItem_t *> tracks;
	if (ctx != DDB_ACTION_CTX_SELECTION && ctx != DDB_ACTION_CTX_PLAYLIST) return tracks;

	ddb_playlist_t * plt = deadbeef->action_get_playlist();
	if (plt == nullptr) return tracks;
	deadbeef->pl_lock();
	DB_playItem_t * it = deadbeef->plt_get_first(plt, PL_MAIN);
	while (it != nullptr)
	{
		if (ctx == DDB_ACTION_CTX_PLAYLIST || deadbeef->pl_is_selected(it))
		{
			deadbeef->pl_item_ref(it);
			tracks.push_back(it);
		}
		DB_playItem_t * next = deadbeef->pl_get_next(it, PL_MAIN);
		deadbeef->pl_item_unref(it);
		it = next;
	}
	deadbeef->pl_unlock();
	deadbeef->plt_unref(plt);
	return tracks;
}

int start_batch(int ctx, job_kind kind, double scale)
{
	std::vector<DB_playItem_t *> tracks = action_tracks(ctx);
	if (tracks.empty()) return 0;

	std::shared_ptr<batch> b = std::make_shared<batch>();
	b->kind = kind;
	b->scale = scale;
	b->s = read_settings();
	b->total = tracks.size();

	if (kind != job_scale)
		RUBATO_INFO("analysing %zu track%s, %d at a time.", tracks.size(),
		            tracks.size() == 1 ? "" : "s",
		            std::min(scan_workers(), static_cast<int>(tracks.size())));

	std::vector<job> jobs;
	jobs.reserve(tracks.size());
	for (DB_playItem_t * t : tracks) jobs.push_back(job{ t, b });
	enqueue(jobs);
	return 0;
}

int action_analyse(DB_plugin_action_t *, int ctx)
{
	return start_batch(ctx, job_analyse_and_write, 1.0);
}

int action_analyse_only(DB_plugin_action_t *, int ctx)
{
	return start_batch(ctx, job_analyse_only, 1.0);
}

int action_double(DB_plugin_action_t *, int ctx)
{
	return start_batch(ctx, job_scale, 2.0);
}

int action_halve(DB_plugin_action_t *, int ctx)
{
	return start_batch(ctx, job_scale, 0.5);
}

DB_plugin_action_t act_halve;
DB_plugin_action_t act_double;
DB_plugin_action_t act_analyse_only;
DB_plugin_action_t act_analyse;

DB_plugin_action_t * get_actions(DB_playItem_t *)
{
	return &act_analyse;
}

void init_action(DB_plugin_action_t & a, const char * title, const char * name, uint32_t flags,
                 DB_plugin_action_callback2_t callback, DB_plugin_action_t * next)
{
	std::memset(&a, 0, sizeof(a));
	a.title = title;
	a.name = name;
	a.flags = flags;
	a.callback2 = callback;
	a.next = next;
}

// --- lifetime --------------------------------------------------------------

int plugin_start()
{
	std::lock_guard<std::mutex> guard(queue_lock);
	stopping = false;
	aborting = false;
	return 0;
}

//! Abandons whatever is still queued - a track half analysed is not written -
//! and waits for the threads, so none of them outlives the player's API.
int plugin_stop()
{
	aborting = true;
	std::vector<std::thread> threads;
	{
		std::lock_guard<std::mutex> guard(queue_lock);
		stopping = true;
		threads.swap(pool);
	}
	queue_changed.notify_all();
	for (std::thread & t : threads) t.join();

	std::deque<job> left;
	{
		std::lock_guard<std::mutex> guard(queue_lock);
		left.swap(queue);
	}
	for (job & j : left) deadbeef->pl_item_unref(j.track);
	return 0;
}

const char plugin_description[] =
	"Detects the tempo of a track and which of Tango, Vals, Milonga or Reggae "
	"it is - or none of them - from the audio alone, and measures its tuning "
	"and key.\n\n"
	"Select tracks and use Rubato in the context menu. Results appear in the "
	"log window (View > Log), one line per track.\n\n"
	"Written fields: BPM (TBPM in an mp3, tmpo in an m4a), INITIALBPM, "
	"BpmAlgorithm, KEY, KEYCANDIDATES, KEYCONFIDENCE, MODEBALANCE, the "
	"container's initial-key slot and KeyAlgorithm, TUNING and "
	"TuningAlgorithm, RETUNE and RETUNECANDIDATES, and GENRE where it was "
	"empty and the rhythm is at least 98% certain.";

const char plugin_copyright[] =
	"Rubato BPM Analyzer " RUBATO_VERSION "\n"
	"\n"
	"MIT License\n"
	"\n"
	"Copyright (c) 2009-2014 Michael Balzer\n"
	"Copyright (c) 2014 Holger Stenger\n"
	"Copyright (c) 2026 Nick Shaforostov\n"
	"\n"
	"Permission is hereby granted, free of charge, to any person obtaining a copy "
	"of this software and associated documentation files (the \"Software\"), to deal "
	"in the Software without restriction, including without limitation the rights "
	"to use, copy, modify, merge, publish, distribute, sublicense, and/or sell "
	"copies of the Software, and to permit persons to whom the Software is "
	"furnished to do so, subject to the following conditions:\n"
	"\n"
	"The above copyright notice and this permission notice shall be included in all "
	"copies or substantial portions of the Software.\n"
	"\n"
	"THE SOFTWARE IS PROVIDED \"AS IS\", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR "
	"IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, "
	"FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE "
	"AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER "
	"LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, "
	"OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE "
	"SOFTWARE.\n"
	"\n"
	FOO_RUBATO_FFT_LICENSE;

}   // namespace

void rubato::wait_until_idle()
{
	std::unique_lock<std::mutex> guard(queue_lock);
	queue_changed.wait(guard, [] { return stopping || (queue.empty() && busy == 0); });
}

extern "C" RUBATO_EXPORT DB_plugin_t * ddb_rubato_load(DB_functions_t * api)
{
	deadbeef = api;

	// The context menu lists these in the order of the chain. Double and
	// halve are kept off the playlist tab's menu: rescaling every BPM in a
	// playlist at once is not a thing to offer next to "Rename".
	const uint32_t tracks = DB_ACTION_SINGLE_TRACK | DB_ACTION_MULTIPLE_TRACKS | DB_ACTION_ADD_MENU;
	init_action(act_halve, "Rubato/Halve BPM", "rubato_halve",
	            tracks | DB_ACTION_EXCLUDE_FROM_CTX_PLAYLIST, action_halve, nullptr);
	init_action(act_double, "Rubato/Double BPM", "rubato_double",
	            tracks | DB_ACTION_EXCLUDE_FROM_CTX_PLAYLIST, action_double, &act_halve);
	init_action(act_analyse_only, "Rubato/Analyse without writing tags", "rubato_analyse_only",
	            tracks, action_analyse_only, &act_double);
	init_action(act_analyse, "Rubato/Analyse BPM, key and tuning", "rubato_analyse",
	            tracks, action_analyse, &act_analyse_only);

	std::memset(&plugin, 0, sizeof(plugin));
	plugin.plugin.type = DB_PLUGIN_MISC;
	plugin.plugin.api_vmajor = 1;
	plugin.plugin.api_vminor = DDB_API_LEVEL;
	plugin.plugin.version_major = RUBATO_VERSION_MAJOR;
	plugin.plugin.version_minor = RUBATO_VERSION_MINOR;
	plugin.plugin.flags = DDB_PLUGIN_FLAG_LOGGING;
	plugin.plugin.id = "rubato";
	plugin.plugin.name = "Rubato BPM Analyzer";
	plugin.plugin.descr = plugin_description;
	plugin.plugin.copyright = plugin_copyright;
	plugin.plugin.website = "https://github.com/shaforostoff/foo_rubato";
	plugin.plugin.start = plugin_start;
	plugin.plugin.stop = plugin_stop;
	plugin.plugin.get_actions = get_actions;
	plugin.plugin.configdialog = settings_dialog;
	return &plugin.plugin;
}
