// Rubato BPM Analyzer for DeaDBeeF.
//
// The same analysis as foo_rubato - bpmcore, untouched - behind DeaDBeeF's
// plugin API instead of foobar2000's: decode a track once, measure the tempo,
// the rhythm, the tuning and the key, and write the same fields under the same
// rules. See rubato_format.h for where the field names differ.
//
// This file is the plugin without its windows: the actions, the settings, the
// worker threads, decoding and writing. The windows are behind rubato_ui.h,
// one implementation per toolkit, and what they show is in rubato_results.h.
// Built without a toolkit, or run under an interface the windows do not
// belong to, a scan writes as soon as it finishes and reports in the log.

#include "rubato_plugin.h"
#include "rubato_format.h"
#include "rubato_results.h"
#include "rubato_ui.h"
#include "rubato_version.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <deque>
#include <map>
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

using rubato::settings;
using rubato::track_result;

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
// One line per track at the info layer: without the results window, that line
// is the result. Failures go to the default layer, which DeaDBeeF's interfaces
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

const char settings_dialog_common[] =
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

// Only where there are windows for them to be about: a build without any
// would be offering switches that do nothing.
const char settings_dialog_windows[] =
	"property \"Write tags without showing the results window\" checkbox rubato.auto_write 0;\n"
	"property \"Taps to average\" entry rubato.taps_to_average 30;\n"
	"property \"Seconds of pause that restart the average\" entry rubato.seconds_to_reset 5;\n";

std::string settings_dialog;

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

//! The title, or the file name where there is none.
std::string title_of(DB_playItem_t * track)
{
	const std::string title = meta(track, "title");
	return title.empty() ? file_name(meta(track, ":URI")) : title;
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
		                          rubato::format_bpm(r.bpm, precision).c_str());
		// In an mp3 the default name moved onto TBPM; a TXXX:BPM left beside
		// it from another tagger would be a second, stale answer.
		if (std::strcmp(bpm_field, s.bpm_tag.c_str()) != 0)
			deadbeef->pl_delete_meta(track, s.bpm_tag.c_str());

		// Only a BPM the analysis stands behind is stamped. One the user
		// doubled or halved in the results window loses its attribution, and
		// that is not the settings page's to switch off: switching it off
		// stops the plugin adding one, not removing a claim that is false.
		if (r.adjusted) deadbeef->pl_delete_meta(track, tag_bpm_algorithm);
		else if (s.write_algorithm)
			deadbeef->pl_replace_meta(track, tag_bpm_algorithm, RUBATO_ALGORITHM);

		std::string initial;
		if (r.initial_bpm > 0) initial = rubato::format_bpm(r.initial_bpm, precision);
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

// --- the work queue --------------------------------------------------------

enum job_kind
{
	job_analyse_and_write,   //!< analyse, then write straight away
	job_analyse_for_review,  //!< analyse, then put up the results window
	job_analyse_only,        //!< analyse, report, write nothing
	job_write_reviewed,      //!< write what the results window was showing
	job_scale,               //!< double or halve the BPM already there
	job_tap                  //!< write a tapped BPM
};

std::atomic<bool> aborting(false);

//! One action's worth of tracks: what it was asked for, and how it is going.
struct batch : public rubato::scan_progress
{
	job_kind kind = job_analyse_and_write;
	double value = 1.0;   //!< job_scale's factor, job_tap's BPM
	settings s;
	std::size_t total = 0;
	std::atomic<std::size_t> done{0};
	std::atomic<std::size_t> written{0};
	std::atomic<std::size_t> failed{0};
	std::atomic<bool> cancelled{false};
	std::chrono::steady_clock::time_point started = std::chrono::steady_clock::now();

	mutable std::mutex lock;
	//! Tracks being worked on, by job index: their names and how far through.
	std::map<std::size_t, std::pair<std::string, double>> in_flight;
	//! job_analyse_for_review: one row per job, track null where it failed.
	std::vector<rubato::results::row> review;
	//! job_write_reviewed: what to write, per job.
	std::vector<track_result> to_write;

	//! A scan stopped before its results window went up still holds a
	//! reference for every row it filled in.
	~batch() override
	{
		for (rubato::results::row & r : review)
			if (r.track != nullptr) deadbeef->pl_item_unref(r.track);
	}

	bool stopped() const { return cancelled.load() || aborting.load(); }

	snapshot read() const override
	{
		snapshot out;
		out.total = total;
		out.done = done.load();
		out.finished = out.done >= total;
		std::lock_guard<std::mutex> guard(lock);
		double partial = 0;
		std::size_t named = 0;
		for (const auto & f : in_flight)
		{
			partial += f.second.second;
			if (named < 2)
			{
				if (named > 0) out.in_flight += in_flight.size() == 2 ? " and " : ", ";
				out.in_flight += f.second.first;
			}
			named++;
		}
		if (in_flight.size() > 2)
			out.in_flight += " and " + std::to_string(in_flight.size() - 2) + " more";
		out.fraction = total == 0 ? 1.0 : (static_cast<double>(out.done) + partial) / total;
		return out;
	}

	void cancel() override { cancelled = true; }

	void set_fraction(std::size_t index, double f)
	{
		std::lock_guard<std::mutex> guard(lock);
		auto it = in_flight.find(index);
		if (it != in_flight.end()) it->second.second = f;
	}
};

struct job
{
	DB_playItem_t * track;   //!< holds a reference
	std::shared_ptr<batch> owner;
	std::size_t index;       //!< within the batch
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

// --- decoding --------------------------------------------------------------

//! Where bpmcore's progress and its question about stopping go.
class core_listener : public bpmcore::listener
{
public:
	core_listener(batch & b, std::size_t index) : m_batch(b), m_index(index) {}
	bool cancelled() override { return m_batch.stopped(); }
	// Decoding took the first 80%; the analysis is the rest.
	void progress(double fraction) override { m_batch.set_fraction(m_index, 0.8 + 0.2 * fraction); }

private:
	batch & m_batch;
	std::size_t m_index;
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
                                     batch & b, std::size_t index, int analysis_threads,
                                     bool & ok)
{
	ok = false;
	bpmcore::analysis result;
	const settings & s = b.s;

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

	const double expected = deadbeef->pl_get_item_duration(track);
	const double expected_frames =
		std::min(expected, bpmcore::max_seconds()) * in_fmt.samplerate;
	double decoded_frames = 0;

	bpmcore::collector collector(static_cast<unsigned>(in_fmt.samplerate), expected);
	for (;;)
	{
		if (b.stopped()) break;
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
		decoded_frames += frames;
		if (expected_frames > 0)
			b.set_fraction(index, 0.8 * std::min(1.0, decoded_frames / expected_frames));
		if (collector.full() || bytes < static_cast<int>(raw.size())) break;
	}
	dec->free(info);

	if (b.stopped()) return result;
	if (collector.size() == 0)
	{
		RUBATO_ERROR("no audio decoded from %s.", name.c_str());
		return result;
	}
	ok = true;

	const auto decoded = std::chrono::steady_clock::now();
	core_listener listener(b, index);
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

// --- running jobs ----------------------------------------------------------

void finish_batch(batch & b)
{
	const double seconds = std::chrono::duration<double>(
		std::chrono::steady_clock::now() - b.started).count();
	const char * plural = b.total == 1 ? "" : "s";

	if (b.stopped())
	{
		if (b.kind != job_scale && b.kind != job_tap)
			RUBATO_INFO("stopped after %zu of %zu track%s; %zu written.",
			            b.done.load(), b.total, plural, b.written.load());
	}
	else if (b.kind == job_analyse_only)
	{
		RUBATO_INFO("analysed %zu track%s in %.1fs; nothing written.", b.total, plural, seconds);
	}
	else if (b.kind == job_analyse_for_review)
	{
		std::vector<rubato::results::row> rows;
		for (rubato::results::row & r : b.review)
			if (r.track != nullptr) rows.push_back(std::move(r));
		b.review.clear();
		RUBATO_INFO("analysed %zu of %zu track%s in %.1fs.", rows.size(), b.total, plural, seconds);
		if (!rows.empty())
			rubato::ui::show_results(std::make_shared<rubato::results>(b.s, std::move(rows)));
	}
	else if (b.kind != job_tap)
	{
		RUBATO_INFO("%zu of %zu track%s written in %.1fs%s.", b.written.load(), b.total,
		            plural, seconds, b.failed.load() > 0 ? "; see above for the rest" : "");
	}

	// The playlists hold a copy of every field; save them so the new values
	// are there after a restart without the files being read again.
	if (b.written.load() > 0) deadbeef->pl_save_all();
}

void finish_job(const job & j)
{
	{
		std::lock_guard<std::mutex> guard(j.owner->lock);
		j.owner->in_flight.erase(j.index);
	}
	deadbeef->pl_item_unref(j.track);
	if (j.owner->done.fetch_add(1) + 1 == j.owner->total) finish_batch(*j.owner);
}

//! Double or halve the BPM a file already has, or write one tapped by hand.
void run_hand_edit(const job & j, const std::string & path, const std::string & name)
{
	batch & b = *j.owner;
	const rubato::container c = rubato::container_of(path.c_str());
	const char * bpm_field = rubato::bpm_field(c, b.s.bpm_tag.c_str());
	const int precision = rubato::bpm_precision_for(c, b.s.precision);

	DB_decoder_t * dec = writer_for(j.track, name);
	if (dec == nullptr) { b.failed++; return; }

	bool changed = false;
	deadbeef->pl_lock();
	if (b.kind == job_tap)
	{
		deadbeef->pl_replace_meta(j.track, bpm_field, rubato::format_bpm(b.value, precision).c_str());
		if (std::strcmp(bpm_field, b.s.bpm_tag.c_str()) != 0)
			deadbeef->pl_delete_meta(j.track, b.s.bpm_tag.c_str());
		// No analysis produced this, and a tap has no opening tempo in it.
		// The key and the tuning are still what was measured, and stay.
		deadbeef->pl_delete_meta(j.track, tag_bpm_algorithm);
		deadbeef->pl_delete_meta(j.track, tag_initial_bpm);
		changed = true;
	}
	else
	{
		double bpm = 0;
		if (rubato::parse_decimal(deadbeef->pl_find_meta(j.track, bpm_field), bpm) && bpm > 0)
		{
			deadbeef->pl_replace_meta(j.track, bpm_field,
			                          rubato::format_bpm(bpm * b.value, precision).c_str());
			// The user overruling the measurement: the analysis no longer
			// stands behind the number. KeyAlgorithm stays - a corrected
			// metrical level says nothing about the key.
			deadbeef->pl_delete_meta(j.track, tag_bpm_algorithm);
			// The opening tempo was read at the same wrong level, so the
			// same factor puts it right.
			double initial = 0;
			if (rubato::parse_decimal(deadbeef->pl_find_meta(j.track, tag_initial_bpm), initial)
			    && initial > 0)
				deadbeef->pl_replace_meta(j.track, tag_initial_bpm,
				                          rubato::format_bpm(initial * b.value, precision).c_str());
			changed = true;
		}
	}
	deadbeef->pl_unlock();

	if (!changed)
	{
		RUBATO_INFO("%s has no %s to scale.", name.c_str(), bpm_field);
		b.failed++;
		return;
	}
	if (commit(dec, j.track, name))
	{
		b.written++;
		if (b.kind == job_tap)
			RUBATO_INFO("%s: %s BPM, tapped.", name.c_str(),
			            rubato::format_bpm(b.value, precision).c_str());
	}
	else b.failed++;
}

void run_write_reviewed(const job & j, const std::string & path, const std::string & name)
{
	batch & b = *j.owner;
	DB_decoder_t * dec = writer_for(j.track, name);
	if (dec == nullptr) { b.failed++; return; }
	deadbeef->pl_lock();
	apply_analysis(j.track, path, b.to_write[j.index], b.s);
	deadbeef->pl_unlock();
	if (commit(dec, j.track, name)) b.written++;
	else b.failed++;
}

void run_analysis(const job & j, const std::string & path, const std::string & name,
                  int analysis_threads)
{
	batch & b = *j.owner;
	const rubato::container c = rubato::container_of(path.c_str());
	const std::string had = meta(j.track, rubato::bpm_field(c, b.s.bpm_tag.c_str()));

	if (b.kind != job_analyse_only && b.s.skip_tagged && !had.empty())
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
	r.analysis = decode_and_analyse(j.track, name, b, j.index, analysis_threads, decoded);
	if (b.stopped()) return;
	if (!decoded) { b.failed++; return; }
	r.bpm = r.analysis.bpm;
	r.initial_bpm = r.analysis.initial_bpm;
	r.spread = r.analysis.bpm_spread;
	r.year = year_of(j.track);

	std::string line = summary(r);
	if (b.kind != job_analyse_and_write && !had.empty()) line += "; tag had " + had;
	RUBATO_INFO("[%zu/%zu] %s: %s", b.done.load() + 1, b.total, name.c_str(), line.c_str());

	if (b.kind == job_analyse_for_review)
	{
		rubato::results::row & row = b.review[j.index];
		deadbeef->pl_item_ref(j.track);   // the results window's
		row.track = j.track;
		row.title = title_of(j.track);
		row.path = path;
		row.tag_bpm = had;
		row.result = r;
		return;
	}
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

		if (!j.owner->stopped())
		{
			const std::string path = meta(j.track, ":URI");
			const std::string name = file_name(path);
			{
				std::lock_guard<std::mutex> guard(j.owner->lock);
				j.owner->in_flight[j.index] = std::make_pair(name, 0.0);
			}
			try
			{
				switch (j.owner->kind)
				{
				case job_scale:
				case job_tap:            run_hand_edit(j, path, name); break;
				case job_write_reviewed: run_write_reviewed(j, path, name); break;
				default:                 run_analysis(j, path, name, analysis_threads); break;
				}
			}
			catch (const std::exception & e)
			{
				RUBATO_ERROR("error analysing %s: %s", name.c_str(), e.what());
				j.owner->failed++;
			}
		}
		finish_job(j);

		{
			std::lock_guard<std::mutex> guard(queue_lock);
			busy--;
		}
		queue_changed.notify_all();
	}
}

//! Queues one job per track, each holding a reference the job gives back.
std::shared_ptr<batch> start(std::shared_ptr<batch> b, const std::vector<DB_playItem_t *> & tracks)
{
	b->total = tracks.size();
	if (b->kind == job_analyse_for_review) b->review.resize(tracks.size());
	{
		std::lock_guard<std::mutex> guard(queue_lock);
		if (stopping) return nullptr;
		if (pool.empty())
		{
			const int n = scan_workers();
			for (int i = 0; i < n; i++) pool.emplace_back(worker);
		}
		for (std::size_t i = 0; i < tracks.size(); i++)
		{
			deadbeef->pl_item_ref(tracks[i]);
			queue.push_back(job{ tracks[i], b, i });
		}
	}
	queue_changed.notify_all();
	return b;
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

void release(std::vector<DB_playItem_t *> & tracks)
{
	for (DB_playItem_t * t : tracks) deadbeef->pl_item_unref(t);
	tracks.clear();
}

int start_action(int ctx, job_kind kind, double value)
{
	std::vector<DB_playItem_t *> tracks = action_tracks(ctx);
	if (tracks.empty()) return 0;

	std::shared_ptr<batch> b = std::make_shared<batch>();
	b->s = rubato::read_settings();
	b->value = value;
	b->kind = kind;
	// The results window where there is one to show, unless the settings
	// say to write without it.
	const bool windows = rubato::ui::available();
	if (kind == job_analyse_and_write && windows && !b->s.auto_write)
		b->kind = job_analyse_for_review;

	if (kind != job_scale)
		RUBATO_INFO("analysing %zu track%s, %d at a time.", tracks.size(),
		            tracks.size() == 1 ? "" : "s",
		            std::min(scan_workers(), static_cast<int>(tracks.size())));

	b = start(b, tracks);
	release(tracks);
	if (b != nullptr && windows && kind != job_scale) rubato::ui::show_progress(b);
	return 0;
}

int action_analyse(DB_plugin_action_t *, int ctx) { return start_action(ctx, job_analyse_and_write, 1.0); }
int action_analyse_only(DB_plugin_action_t *, int ctx) { return start_action(ctx, job_analyse_only, 1.0); }
int action_double(DB_plugin_action_t *, int ctx) { return start_action(ctx, job_scale, 2.0); }
int action_halve(DB_plugin_action_t *, int ctx) { return start_action(ctx, job_scale, 0.5); }

int action_tap(DB_plugin_action_t *, int)
{
	rubato::ui::show_tap();
	return 0;
}

DB_plugin_action_t act_analyse;
DB_plugin_action_t act_analyse_only;
DB_plugin_action_t act_double;
DB_plugin_action_t act_halve;
DB_plugin_action_t act_tap;

void init_action(DB_plugin_action_t & a, const char * title, const char * name, uint32_t flags,
                 DB_plugin_action_callback2_t callback)
{
	std::memset(&a, 0, sizeof(a));
	a.title = title;
	a.name = name;
	a.flags = flags;
	a.callback2 = callback;
}

//! Asked again every time a menu is built, which is what lets the menu follow
//! whether there are windows: with a results window to look at before
//! writing, analysing without writing has nothing to add, and without one
//! there is no tapping window to open.
DB_plugin_action_t * get_actions(DB_playItem_t *)
{
	const bool windows = rubato::ui::available();
	DB_plugin_action_t * chain[] = {
		&act_analyse,
		windows ? nullptr : &act_analyse_only,
		&act_double,
		&act_halve,
		windows ? &act_tap : nullptr
	};
	DB_plugin_action_t * head = nullptr;
	DB_plugin_action_t ** link = &head;
	for (DB_plugin_action_t * a : chain)
	{
		if (a == nullptr) continue;
		*link = a;
		link = &a->next;
	}
	*link = nullptr;
	return head;
}

// --- lifetime --------------------------------------------------------------

int plugin_start()
{
	std::lock_guard<std::mutex> guard(queue_lock);
	stopping = false;
	aborting = false;
	return 0;
}

int plugin_connect()
{
	rubato::ui::connect(deadbeef);
	return 0;
}

//! Abandons whatever is still queued - a track half analysed is not written -
//! and waits for the threads, so none of them outlives the player's API.
int plugin_stop()
{
	aborting = true;
	rubato::ui::shutdown();
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
	"Select tracks and use Rubato in the context menu. Each track gets a line "
	"in the log window (View > Log).\n\n"
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

// --- rubato_results.h: the parts that reach the player -------------------

rubato::settings rubato::read_settings()
{
	settings s;
	s.precision = deadbeef->conf_get_int("rubato.bpm_precision", 0);
	char tag[128];
	deadbeef->conf_get_str("rubato.bpm_tag", "BPM", tag, sizeof(tag));
	// An empty name would write the BPM nowhere and remove nothing.
	if (tag[0] != '\0') s.bpm_tag = tag;
	s.write_initial    = deadbeef->conf_get_int("rubato.write_initial_bpm", 1) != 0;
	s.write_algorithm  = deadbeef->conf_get_int("rubato.write_algorithm", 1) != 0;
	s.detect_key       = deadbeef->conf_get_int("rubato.detect_key", 1) != 0;
	s.write_key        = deadbeef->conf_get_int("rubato.write_key", 1) != 0;
	s.write_tuning     = deadbeef->conf_get_int("rubato.write_tuning", 1) != 0;
	s.write_retune     = deadbeef->conf_get_int("rubato.write_retune", 1) != 0;
	s.skip_tagged      = deadbeef->conf_get_int("rubato.skip_tagged", 0) != 0;
	s.diagnostics      = deadbeef->conf_get_int("rubato.diagnostics", 0) != 0;
	s.auto_write       = deadbeef->conf_get_int("rubato.auto_write", 0) != 0;
	// Two taps are the fewest that make an interval, and a second is the
	// shortest pause that is not simply a slow beat.
	s.taps_to_average  = std::max(2, deadbeef->conf_get_int("rubato.taps_to_average", 30));
	s.seconds_to_reset = std::max(1, deadbeef->conf_get_int("rubato.seconds_to_reset", 5));
	return s;
}

rubato::results::~results()
{
	for (row & r : m_rows) deadbeef->pl_item_unref(r.track);
}

void rubato::results::commit()
{
	if (m_committed || m_rows.empty()) return;
	m_committed = true;

	std::shared_ptr<batch> b = std::make_shared<batch>();
	b->kind = job_write_reviewed;
	b->s = m_settings;
	std::vector<DB_playItem_t *> tracks;
	for (const row & r : m_rows)
	{
		tracks.push_back(r.track);
		b->to_write.push_back(r.result);
	}
	start(b, tracks);
}

std::string rubato::playing_title()
{
	DB_playItem_t * it = deadbeef->streamer_get_playing_track();
	if (it == nullptr) return std::string();
	const std::string title = title_of(it);
	deadbeef->pl_item_unref(it);
	return title;
}

bool rubato::write_tapped_bpm(double bpm)
{
	if (!(bpm > 0)) return false;
	DB_playItem_t * it = deadbeef->streamer_get_playing_track();
	if (it == nullptr) return false;
	std::shared_ptr<batch> b = std::make_shared<batch>();
	b->kind = job_tap;
	b->value = bpm;
	b->s = read_settings();
	std::vector<DB_playItem_t *> tracks(1, it);
	const bool queued = start(b, tracks) != nullptr;
	release(tracks);
	return queued;
}

void rubato::wait_until_idle()
{
	std::unique_lock<std::mutex> guard(queue_lock);
	queue_changed.wait(guard, [] { return stopping || (queue.empty() && busy == 0); });
}

extern "C" RUBATO_EXPORT DB_plugin_t * ddb_rubato_load(DB_functions_t * api)
{
	deadbeef = api;

	// Double and halve are kept off the playlist tab's menu: rescaling every
	// BPM in a playlist at once is not a thing to offer next to "Rename". The
	// tapping window is about what is playing, not about the selection, and
	// is offered wherever the menu is.
	const uint32_t tracks = DB_ACTION_SINGLE_TRACK | DB_ACTION_MULTIPLE_TRACKS | DB_ACTION_ADD_MENU;
	init_action(act_analyse, "Rubato/Analyse BPM, key and tuning", "rubato_analyse",
	            tracks, action_analyse);
	init_action(act_analyse_only, "Rubato/Analyse without writing tags", "rubato_analyse_only",
	            tracks, action_analyse_only);
	init_action(act_double, "Rubato/Double BPM", "rubato_double",
	            tracks | DB_ACTION_EXCLUDE_FROM_CTX_PLAYLIST, action_double);
	init_action(act_halve, "Rubato/Halve BPM", "rubato_halve",
	            tracks | DB_ACTION_EXCLUDE_FROM_CTX_PLAYLIST, action_halve);
	init_action(act_tap, "Rubato/Tap BPM of the playing track...", "rubato_tap",
	            tracks | DB_ACTION_EXCLUDE_FROM_CTX_PLAYLIST, action_tap);

	settings_dialog = settings_dialog_common;
#if RUBATO_HAVE_WINDOWS
	settings_dialog += settings_dialog_windows;
#else
	(void) settings_dialog_windows;
#endif

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
	plugin.plugin.connect = plugin_connect;
	plugin.plugin.get_actions = get_actions;
	plugin.plugin.configdialog = settings_dialog.c_str();
	return &plugin.plugin;
}
