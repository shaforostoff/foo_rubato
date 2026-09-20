#include "stdafx.h"
#include "bpm_analysis.h"
#include "preferences.h"

#include <chrono>

/***** Analysis entry point *****/
//
// One pass over the whole track: onset envelope, windowed autocorrelation,
// metrical grid, rhythm class, then the metrical level a dancer would tap.
// Everything here is host plumbing - decoding, progress, the console line -
// and the analysis itself is bpmcore's.

namespace
{
	//! Bridges foobar2000's abort and this component's progress reporting into
	//! the core.
	class core_listener : public bpmcore::listener
	{
	public:
		core_listener(bpm_analysis_progress & progress, abort_callback & abort)
			: m_progress(progress), m_abort(abort) {}

		bool cancelled() override { return m_abort.is_aborting(); }
		void progress(double fraction) override
		{
			// Decoding took the first quarter; the envelope, which is most of
			// what is left, is scaled into the other three.
			m_progress.fraction(0.25 + fraction * 0.75);
		}

	private:
		bpm_analysis_progress & m_progress;
		abort_callback & m_abort;
	};
}

bpmcore::analysis bpm_analyse(const metadb_handle_ptr & track,
                              bpm_analysis_progress & progress,
                              abort_callback & abort,
                              int analysis_threads)
{
	bpmcore::analysis result;

	const auto started = std::chrono::steady_clock::now();

	input_helper input;
	service_ptr_t<file> nothing;
	// The whole side is read once, front to back, and never seeked. Saying
	// so lets a decoder skip building a seektable it will not be asked for,
	// and stops a format that carries looping metadata from being decoded
	// round and round until the length cap stops it.
	input.open(nothing, track, input_flag_simpledecode, abort, false, false);
	if (!input.is_open())
	{
		FB2K_console_formatter() << "foo_rubato: could not open " << track->get_path() << " for analysis.";
		return result;
	}

	progress.fraction(0);

	// The envelope has to be normalised by the track's overall level before
	// it is compressed, and that is not known until the whole side has been
	// decoded, so the audio is collected rather than streamed through.
	std::unique_ptr<bpmcore::collector> collector;
	unsigned sample_rate = 0;
	audio_chunk_impl chunk;
	while (input.run(chunk, abort))
	{
		abort.check();

		const unsigned channels = chunk.get_channels();
		const unsigned srate = chunk.get_srate();
		if (channels == 0 || srate == 0) continue;

		if (collector == nullptr)
		{
			sample_rate = srate;
			// The length the metadb already holds, so the buffer is sized
			// for this side rather than for a nominal four minutes.
			collector.reset(new bpmcore::collector(srate, track->get_length()));
		}
		else if (srate != sample_rate)
		{
			// A file whose rate changes mid-stream would put the frame grid
			// on two different time bases; the tempo would be meaningless.
			FB2K_console_formatter() << "foo_rubato: sample rate changes within "
			                         << track->get_path() << "; analysis stopped at the change.";
			break;
		}

		collector->add_interleaved(chunk.get_data(), chunk.get_sample_count(), channels);
		if (collector->full()) break;
	}

	if (collector == nullptr || collector->size() == 0)
	{
		FB2K_console_formatter() << "foo_rubato: no audio decoded from " << track->get_path() << ".";
		return result;
	}

	progress.fraction(0.25);
	const auto decoded = std::chrono::steady_clock::now();

	core_listener listener(progress, abort);
	bpmcore::options options;
	options.threads = analysis_threads;
	// Roughly doubles the analysis when it is on, which is still a small
	// fraction of the decode above - and the decode is what a library scan
	// actually costs.
	options.detect_key = bpm_config_detect_key;
	result = collector->finish(&listener, &options);
	abort.check();
	const auto analysed = std::chrono::steady_clock::now();

	// Two numbers rather than one, because they have nothing to do with each
	// other and only one of them is this component's to fix. Reading a track
	// is the decoder's cost and dwarfs the rest on a slow codec or a network
	// share; the analysis runs at hundreds of times realtime. Both are wall
	// clock on this thread, so with several tracks in flight they include
	// whatever time the thread spent waiting for a core.
	const double read_seconds =
		std::chrono::duration<double>(decoded - started).count();
	const double analysis_seconds =
		std::chrono::duration<double>(analysed - decoded).count();

	if (!result.ok)
	{
		FB2K_console_formatter() << "foo_rubato: could not measure a tempo in " << track->get_path()
		                         << " (" << pfc::format_float(result.duration, 0, 1) << "s decoded).";
		// Not an early return any more: the key and the tuning are measured
		// from their own pass and stand or fall on their own, so a side whose
		// tempo the grid never settled on can still come back with a key.
	}

	if (bpm_config_output_debug && result.key.ok)
	{
		const bpmcore::key_analysis & k = result.key;
		pfc::string_formatter mode;
		if (k.major_fraction >= 0)
			mode << ", " << pfc::format_float(100.0 * k.major_fraction, 0, 0)
			     << "% major over " << k.mode_windows << " windows";
		FB2K_console_formatter() << "foo_rubato: " << pfc::string_filename_ext(track->get_path())
			<< " -> key " << bpmcore::key_name(k.best.root, k.best.minor)
			<< " (" << bpmcore::key_confidence_name(k.confidence)
			<< ", margin " << pfc::format_float(k.margin, 0, 3) << "), tuning "
			<< pfc::format_float(k.tuning_cents, 0, 1) << " cents"
			<< " (R=" << pfc::format_float(k.tuning_r, 0, 2) << ")"
			<< (k.near_wrap ? ", near the semitone wrap" : "")
			<< mode;
	}

	if (bpm_config_output_debug && result.ok)
	{
		FB2K_console_formatter() << "foo_rubato: " << pfc::string_filename_ext(track->get_path())
			<< " -> " << pfc::format_float(result.bpm, 0, 2) << " BPM, "
			<< bpmcore::rhythm_name(result.rhythm)
			<< " (p=" << pfc::format_float(result.confidence, 0, 2) << "), beat "
			<< pfc::format_float(result.beat_bpm, 0, 2) << " BPM, " << result.meter << "/4 grid, "
			<< "opens at " << pfc::format_float(result.initial_bpm, 0, 2)
			<< " BPM, fluctuation +/-" << pfc::format_float(result.bpm_spread, 0, 2)
			<< " BPM over " << result.spread_windows << " windows; "
			<< pfc::format_float(result.duration, 0, 1) << "s of audio, "
			<< pfc::format_float(read_seconds, 0, 2) << "s to read, "
			<< pfc::format_float(analysis_seconds, 0, 2) << "s to analyse";
	}

	return result;
}
