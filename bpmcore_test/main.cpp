// Verification and benchmark harness for bpmcore.
//
// It links the analysis and nothing else - no foobar2000, no pfc - which is
// both the point of the split and what lets this run in CI.
//
//   model <cases file>
//       feed stored feature vectors straight to the classifier and check the
//       exported trees reproduce the class and probability scikit-learn gave.
//
//   resample
//       check the resampler, and that one synthesised track analyses the same
//       at every input rate. Needs no audio on disk.
//
//   tempo_spread
//       check the local-tempo spread against click tracks whose fluctuation is
//       known in advance - steady, a linear ramp, a wobble. Needs no audio.
//
//   pipeline <raw f32 mono file> <sample rate>
//       run the whole chain and print the result, for comparison against the
//       Python reference implementation the model was developed with.
//
//   fft_sizes
//       check the size the transform is given is one the backend can take, at
//       every sample rate a file might carry, and that the window it makes is
//       still the duration the model was fitted at. Needs no audio.
//
//   key_synth
//       check the tuning offset, the key and the retune arithmetic against
//       synthesised chords whose answers are known in advance. Needs no audio.
//
//   key <audio file> [year] [threads]
//       print one track's tuning, key candidates and retune suggestion.
//
//   key_batch <list file> [threads]
//       the same for a whole collection, one TSV row each, for scoring
//       against a discography. Each line is a path, optionally a tab and the
//       recording year, which is what the retune suggestion needs.
//
//   batch <list of audio files> [threads]
//       decode and analyse every track in a list file, one TSV row each, so
//       two builds of the analysis can be compared over a whole collection.
//       This is the only mode that needs ffmpeg, and the only one that reads
//       anything but raw PCM.
//
//   bench <raw f32 mono file> <sample rate> [repeats] [threads] [key 0|1]
//       time the analysis. The last argument switches the tuning and key
//       stage off, which is how its share of the run time is measured.
//
//   trajectory <raw f32 mono file> <sample rate>
//       print the tempo measured in each autocorrelation window, which is what
//       the reported spread is the 10th-to-90th half-span of. A real drift
//       walks; a track the windows cannot track scatters.

#include <bpmcore/bpmcore.h>
// The harness reaches past the public interface for the classifier check, so
// that the feature count and the tree walk are not restated here.
#include <bpmcore/internal.h>
#include <bpmcore/real_fft.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

// The width bpmcore's transform was built at. kiss_fft defines this PUBLIC, so
// it arrives without any of its headers being included - the name expands to
// `double` or `float`, both of which are types this can name and size.
#if defined(kiss_fft_scalar)
#define BPMCORE_TEST_STR2(x) #x
#define BPMCORE_TEST_STR(x)  BPMCORE_TEST_STR2(x)
#define BPMCORE_TEST_SCALAR  BPMCORE_TEST_STR(kiss_fft_scalar)
#else
#define BPMCORE_TEST_SCALAR  "unknown"
#endif

namespace
{
	std::string lower(std::string s)
	{
		for (char & c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
		return s;
	}

	bool read_pcm(const char * path, std::vector<float> & out)
	{
		std::ifstream in(path, std::ios::binary);
		if (!in) return false;
		in.seekg(0, std::ios::end);
		const std::streamoff bytes = in.tellg();
		in.seekg(0, std::ios::beg);
		if (bytes <= 0) return false;
		out.assign(static_cast<std::size_t>(bytes) / sizeof(float), 0.0f);
		in.read(reinterpret_cast<char *>(out.data()),
		        static_cast<std::streamsize>(out.size() * sizeof(float)));
		return in.good() || in.eof();
	}

	int run_pipeline(const char * path, unsigned sample_rate, int threads)
	{
		std::vector<float> mono;
		if (!read_pcm(path, mono))
		{
			std::fprintf(stderr, "cannot read %s\n", path);
			return 2;
		}
		bpmcore::options opt; opt.threads = threads;
		const bpmcore::analysis a = bpmcore::analyse(mono.data(), mono.size(), sample_rate, nullptr, &opt);
		if (!a.ok)
		{
			std::fprintf(stderr, "analysis failed\n");
			return 3;
		}
		// bpm rhythm confidence beat_bpm meter duration bpm_spread spread_windows initial_bpm
		std::printf("%.6f %s %.6f %.6f %d %.3f %.4f %d %.6f\n", a.bpm, bpmcore::rhythm_name(a.rhythm),
		            a.confidence, a.beat_bpm, a.meter, a.duration,
		            a.bpm_spread, a.spread_windows, a.initial_bpm);
		return 0;
	}

	//! ffmpeg, found the way scripts/analysis/config.py finds it.
	std::string ffmpeg_path()
	{
		const char * e = std::getenv("TANGO_FFMPEG");
		return e != nullptr && *e != '\0' ? std::string(e) : std::string("ffmpeg");
	}

	//! Decodes one file to mono float at the model rate, through ffmpeg.
	//!
	//! The same invocation `scripts/analysis/odf.py` uses, so a track measured
	//! here and the same track measured by the reference pipeline start from
	//! identical samples. Decoding straight to the model rate also keeps the
	//! resampler out of the comparison: `analyse` passes audio already at that
	//! rate through untouched, so what two builds differ by is the spectral
	//! stage alone rather than the spectral stage plus a resampling.
	//!
	//! To a pipe rather than a cache on disk because a decoded collection is
	//! about 190GB, and the decode is an hour that gets paid twice rather than
	//! twenty times.
	bool decode_track(const std::string & path, const std::string & ffmpeg,
	                  std::vector<float> & out, std::string & error)
	{
		out.clear();
		if (path.find('"') != std::string::npos)
		{
			error = "path contains a quote";
			return false;
		}

		// Bounded here because the free `analyse` does not bound itself - only the
		// collector the component uses does. A mis-tagged hour-long file would
		// otherwise decide how much memory the sweep takes.
		const std::size_t max_samples = static_cast<std::size_t>(
			bpmcore::max_seconds() * bpmcore::odf_model_rate);

		const std::string tail = " -ac 1 -ar " + std::to_string(bpmcore::odf_model_rate)
		                       + " -f f32le -";
#if defined(_WIN32)
		// u8path is what turns a UTF-8 path from the list file into the UTF-16 the
		// wide CRT wants; the collections have accented names, and the narrow
		// _popen would hand those to cmd.exe in the ANSI code page.
		const std::wstring wexe  = std::filesystem::u8path(ffmpeg).wstring();
		const std::wstring wpath = std::filesystem::u8path(path).wstring();
		const std::wstring wtail(tail.begin(), tail.end());   // ASCII by construction
		// The outer pair of quotes is cmd.exe's: it strips the first and last quote
		// of a command that begins with one, which is how a quoted executable and a
		// quoted argument survive in the same command line.
		const std::wstring cmd = L"\"\"" + wexe + L"\" -v error -i \"" + wpath
		                       + L"\"" + wtail + L"\"";
		std::FILE * pipe = _wpopen(cmd.c_str(), L"rb");
#else
		const std::string cmd = "\"" + ffmpeg + "\" -v error -i \"" + path + "\"" + tail;
		std::FILE * pipe = popen(cmd.c_str(), "r");
#endif
		if (pipe == nullptr) { error = "cannot start ffmpeg"; return false; }

		std::vector<char> bytes;
		char buf[1 << 16];
		std::size_t n;
		// Past the cap the pipe is drained rather than abandoned: closing it early
		// would leave ffmpeg writing into a broken pipe, and the exit status is
		// wanted.
		while ((n = std::fread(buf, 1, sizeof buf, pipe)) > 0)
			if (bytes.size() < max_samples * sizeof(float))
				bytes.insert(bytes.end(), buf, buf + n);

#if defined(_WIN32)
		const int rc = _pclose(pipe);
#else
		const int rc = pclose(pipe);
#endif
		if (rc != 0) { error = "ffmpeg exited " + std::to_string(rc); return false; }

		const std::size_t count = std::min(bytes.size() / sizeof(float), max_samples);
		if (count < bpmcore::odf_model_rate) { error = "short or empty decode"; return false; }
		out.resize(count);
		std::memcpy(out.data(), bytes.data(), count * sizeof(float));
		// One NaN poisons the RMS and with it the whole envelope. The reference
		// pipeline calls nan_to_num on the same samples for the same reason.
		for (float & v : out) if (!std::isfinite(v)) v = 0.0f;
		return true;
	}

	//! Every track in a list file, one row each.
	//!
	//! For comparing two builds of the analysis over a whole collection: run it
	//! under each and diff the output. The rows carry the discrete decisions as
	//! well as the tempo, because a change that leaves accuracy against the
	//! ground truth alone can still move individual tracks underneath it - and of
	//! those the metrical level is the one that costs a factor of two when it
	//! flips.
	//!
	//! A track that will not decode still gets a row, so that two runs stay line
	//! for line and a diff shows only what the analysis did.
	//! The size the transform is given, at every rate a file might carry.
	//!
	//! Two things have to hold at once, and the second is why this is a test
	//! rather than a comment. The size must be one the backend can actually
	//! transform - pffft checks that with assert() alone, so a release build
	//! would hand back a setup for a size it cannot do and then produce a wrong
	//! spectrum in silence. And the window it makes must still last about the
	//! 46.4ms the model was fitted at, because pffft's restriction is coarser
	//! than kiss's: sizes are 32 apart rather than 2, so rounding to one can
	//! move the window further than it used to.
	int run_fft_sizes()
	{
		// Every rate `analyse` will see. The ones that are the model rate times a
		// power of two go straight through; the rest are resampled unless no
		// usable ratio exists, and it is that fallback this is really about.
		static const unsigned rates[] = {
			8000, 11025, 12000, 16000, 22050, 24000, 32000, 37800, 44056, 44100,
			47250, 48000, 50000, 50400, 64000, 88200, 96000, 176400, 192000 };

		int failed = 0;
		double worst_error = 0.0;
		unsigned worst_rate = 0;

		for (unsigned rate : rates)
		{
			const int ideal = static_cast<int>(std::lround(bpmcore::odf_window_seconds * rate));
			const int nfft = bpmcore::fft_size_for(ideal);

			if (!bpmcore::fft_size_supported(nfft))
			{
				std::printf("FAIL  %6u Hz  size %d is not one the backend can transform\n",
				            rate, nfft);
				failed++;
				continue;
			}

			const double window_ms = 1000.0 * nfft / rate;
			const double want_ms = 1000.0 * bpmcore::odf_window_seconds;
			const double error = std::fabs(window_ms - want_ms) / want_ms;
			if (error > worst_error) { worst_error = error; worst_rate = rate; }

			std::printf("      %6u Hz  ideal %5d  size %5d  window %5.2fms  (%+.1f%%)\n",
			            rate, ideal, nfft, window_ms, 100.0 * (window_ms - want_ms) / want_ms);
		}

		// Every rate that is the model rate times a power of two lands exactly,
		// under either backend, so this only ever bounds the odd-rate fallback.
		// Measured there: kiss's grid is even 5-smooth sizes and costs at most
		// 3.1%, at 8kHz; pffft's is 5-smooth multiples of 32, coarser, and costs
		// at most 5.0%, at 47250Hz. 8% sits clear of both and well under what a
		// broken size choice would give - plain powers of two would be 37.8% out
		// at 32kHz, which is the regression this is really watching for.
		const double limit = 0.08;
		std::printf("worst window error %.2f%% at %u Hz (limit %.0f%%)\n",
		            100.0 * worst_error, worst_rate, 100.0 * limit);
		if (worst_error > limit)
		{
			std::printf("FAIL  the size restriction moved a window too far\n");
			failed++;
		}

		std::printf("%s\n", failed == 0 ? "all rates give a usable size" : "FAILURES");
		return failed == 0 ? 0 : 1;
	}

	int run_batch(const char * list_path, int threads)
	{
		std::ifstream in(list_path, std::ios::binary);
		if (!in)
		{
			std::fprintf(stderr, "cannot open %s\n", list_path);
			return 2;
		}

		std::vector<std::string> paths;
		std::string line;
		while (std::getline(in, line))
		{
			if (!line.empty() && line.back() == '\r') line.pop_back();
			// A UTF-8 BOM would otherwise become part of the first path.
			if (paths.empty() && line.compare(0, 3, "\xEF\xBB\xBF") == 0) line.erase(0, 3);
			if (line.empty() || line[0] == '#') continue;
			paths.push_back(line);
		}
		if (paths.empty())
		{
			std::fprintf(stderr, "%s lists no files\n", list_path);
			return 2;
		}

		const std::string ffmpeg = ffmpeg_path();
		std::fprintf(stderr, "%d tracks, %s precision, ffmpeg %s\n",
		             static_cast<int>(paths.size()), BPMCORE_TEST_SCALAR, ffmpeg.c_str());

		// Column names only - nothing that identifies the build, so that two runs
		// differ on the rows and not on the header.
		std::printf("#path\tok\tbpm\trhythm\tconfidence\tbeat_bpm\tmeter"
		            "\tinitial_bpm\tbpm_spread\tspread_windows\tduration\n");

		bpmcore::options opt;
		opt.threads = threads;
		int failed = 0;
		std::vector<float> mono;
		for (std::size_t i = 0; i < paths.size(); i++)
		{
			std::string error;
			bpmcore::analysis a;
			if (!decode_track(paths[i], ffmpeg, mono, error))
			{
				std::fprintf(stderr, "%s: %s\n", paths[i].c_str(), error.c_str());
				failed++;
			}
			else
			{
				a = bpmcore::analyse(mono.data(), mono.size(),
				                     bpmcore::odf_model_rate, nullptr, &opt);
				if (!a.ok)
				{
					std::fprintf(stderr, "%s: analysis declined the track\n", paths[i].c_str());
					failed++;
				}
			}

			std::printf("%s\t%d\t%.6f\t%s\t%.6f\t%.6f\t%d\t%.6f\t%.4f\t%d\t%.3f\n",
			            paths[i].c_str(), a.ok ? 1 : 0, a.bpm,
			            a.ok ? bpmcore::rhythm_name(a.rhythm) : "-",
			            a.confidence, a.beat_bpm, a.meter, a.initial_bpm,
			            a.bpm_spread, a.spread_windows, a.duration);
			// Flushed per row so an interrupted sweep still has usable output, and
			// so progress on stderr stays in step with it.
			std::fflush(stdout);
			if ((i + 1) % 25 == 0 || i + 1 == paths.size())
				std::fprintf(stderr, "\r%d/%d", static_cast<int>(i + 1),
				             static_cast<int>(paths.size()));
		}
		std::fprintf(stderr, "\n%d of %d tracks produced no analysis\n",
		             failed, static_cast<int>(paths.size()));
		return 0;
	}

	//! The per-window tempo behind `analysis::bpm_spread`, for inspecting a
	//! figure that looks wrong. Stages the pipeline the way run_profile does,
	//! because the spread is measured from windows the public call discards.
	int run_trajectory(const char * path, unsigned sample_rate)
	{
		std::vector<float> mono;
		if (!read_pcm(path, mono))
		{
			std::fprintf(stderr, "cannot read %s\n", path);
			return 2;
		}

		std::vector<float> converted;
		const float * pcm = mono.data();
		std::size_t count = mono.size();
		unsigned rate = sample_rate;
		if (!bpmcore::rate_matches_model(sample_rate))
		{
			bpmcore::resampler rs(sample_rate, bpmcore::odf_model_rate);
			if (rs.valid())
			{
				rs.convert_all(mono.data(), mono.size(), converted, 0);
				pcm = converted.data();
				count = converted.size();
				rate = rs.rate_out();
			}
		}

		bpmcore::odf o;
		if (!bpmcore::compute_odf(pcm, count, rate, o, nullptr, 0)) return 3;

		std::vector<float> novelty;
		bpmcore::mix_bands(o, novelty);
		bpmcore::make_novelty(novelty, o.frame_rate);

		std::vector<double> acf;
		std::vector<std::vector<double> > per_window;
		bpmcore::autocorrelate(novelty, acf, static_cast<int>(std::lround(5.0 * o.frame_rate)),
		                       o.frame_rate, 0, &per_window);
		if (acf.empty()) return 3;

		const bpmcore::grid g = bpmcore::find_grid(acf, o.frame_rate);
		if (g.beat_lag <= 0) return 3;

		std::vector<double> features;
		bpmcore::build_features(o, novelty, acf, g, features);
		double confidence = 0;
		const int rhythm = bpmcore::classify(features, &confidence);
		const double tapped = bpmcore::tapped_bpm(acf, g.beat_lag, rhythm, g.meter, o.frame_rate);

		int windows = 0;
		std::vector<double> ratios;
		const double rel = bpmcore::local_tempo_spread(per_window, g.beat_lag, &windows, &ratios);

		std::printf("%.1fs  %s  beat %.3f  tapped %.3f  initial %.3f  spread %.4f over %d of %d windows\n",
		            o.duration, bpmcore::rhythm_name(rhythm), bpmcore::lag_to_bpm(g.beat_lag, o.frame_rate),
		            tapped, bpmcore::initial_tempo_ratio(ratios) * tapped,
		            rel * tapped, windows, static_cast<int>(ratios.size()));
		for (std::size_t w = 0; w < ratios.size(); w++)
		{
			const double centre = bpmcore::acf_window_seconds * 0.5
			                    + bpmcore::acf_hop_seconds * static_cast<double>(w);
			if (ratios[w] <= 0)
				std::printf("  %6.1fs        -\n", centre);
			else
				std::printf("  %6.1fs  %8.3f\n", centre, tapped * ratios[w]);
		}
		return 0;
	}

	int run_bench(const char * path, unsigned sample_rate, int repeats, int threads,
	              bool detect_key)
	{
		std::vector<float> mono;
		if (!read_pcm(path, mono))
		{
			std::fprintf(stderr, "cannot read %s\n", path);
			return 2;
		}
		const double audio_seconds = static_cast<double>(mono.size()) / sample_rate;
		double best = 1e18, total = 0;
		bpmcore::analysis a;
		for (int i = 0; i < repeats; i++)
		{
			const auto t0 = std::chrono::steady_clock::now();
			bpmcore::options opt;
			opt.threads = threads;
			opt.detect_key = detect_key;
			a = bpmcore::analyse(mono.data(), mono.size(), sample_rate, nullptr, &opt);
			const double dt = std::chrono::duration<double>(
				std::chrono::steady_clock::now() - t0).count();
			best = std::min(best, dt);
			total += dt;
		}
		std::printf("threads=%d audio=%.1fs best=%.3fs mean=%.3fs realtime=%.0fx bpm=%.2f %s\n",
		            threads, audio_seconds, best, total / repeats, audio_seconds / best,
		            a.bpm, bpmcore::rhythm_name(a.rhythm));
		return 0;
	}

	//! Stage-by-stage timings, to show where the run time actually goes.
	int run_profile(const char * path, unsigned sample_rate, int repeats, int threads)
	{
		std::vector<float> mono;
		if (!read_pcm(path, mono))
		{
			std::fprintf(stderr, "cannot read %s\n", path);
			return 2;
		}
		const double audio = static_cast<double>(mono.size()) / sample_rate;

		// Resampling is a stage now, and one that only runs for some rates.
		double t_res = 1e18, t_odf = 1e18, t_nov = 1e18, t_acf = 1e18;
		double t_grid = 1e18, t_feat = 1e18, t_cls = 1e18;
		auto now = [] { return std::chrono::steady_clock::now(); };
		auto secs = [](std::chrono::steady_clock::time_point a,
		               std::chrono::steady_clock::time_point b)
		{ return std::chrono::duration<double>(b - a).count(); };

		for (int i = 0; i < repeats; i++)
		{
			auto tr0 = now();
			std::vector<float> converted;
			const float * pcm = mono.data();
			std::size_t count = mono.size();
			unsigned rate = sample_rate;
			if (!bpmcore::rate_matches_model(sample_rate))
			{
				bpmcore::resampler rs(sample_rate, bpmcore::odf_model_rate);
				if (rs.valid())
				{
					rs.convert_all(mono.data(), mono.size(), converted, threads);
					pcm = converted.data();
					count = converted.size();
					rate = rs.rate_out();
				}
			}
			auto t0 = now();

			bpmcore::odf o;
			if (!bpmcore::compute_odf(pcm, count, rate, o, nullptr, threads)) return 3;
			auto t1 = now();

			std::vector<float> novelty;
			bpmcore::mix_bands(o, novelty);
			bpmcore::make_novelty(novelty, o.frame_rate);
			auto t2 = now();

			std::vector<double> acf;
			bpmcore::autocorrelate(novelty, acf,
				static_cast<int>(std::lround(5.0 * o.frame_rate)), o.frame_rate, threads);
			auto t3 = now();

			const bpmcore::grid g = bpmcore::find_grid(acf, o.frame_rate);
			auto t4 = now();

			std::vector<double> features;
			bpmcore::build_features(o, novelty, acf, g, features);
			auto t5 = now();

			double conf = 0;
			const int cls = bpmcore::classify(features, &conf);
			bpmcore::tapped_bpm(acf, g.beat_lag, cls, g.meter, o.frame_rate);
			auto t6 = now();

			t_res = std::min(t_res, secs(tr0, t0));
			t_odf = std::min(t_odf, secs(t0, t1));
			t_nov = std::min(t_nov, secs(t1, t2));
			t_acf = std::min(t_acf, secs(t2, t3));
			t_grid = std::min(t_grid, secs(t3, t4));
			t_feat = std::min(t_feat, secs(t4, t5));
			t_cls = std::min(t_cls, secs(t5, t6));
		}
		const double sum = t_res + t_odf + t_nov + t_acf + t_grid + t_feat + t_cls;
		std::printf("audio=%.1fs threads=%d total=%.4fs (%.0fx realtime)\n",
		            audio, threads, sum, audio / sum);
		const char * names[] = { "resample", "envelope", "novelty", "autocorr",
		                         "grid", "features", "classify" };
		const double times[] = { t_res, t_odf, t_nov, t_acf, t_grid, t_feat, t_cls };
		for (int i = 0; i < 7; i++)
			std::printf("  %-9s %7.4fs  %5.1f%%\n", names[i], times[i], 100.0 * times[i] / sum);
		return 0;
	}

	const double test_pi = 3.14159265358979323846;

	//! A band-limited beat pattern, evaluated from time rather than sampled, so
	//! the same music can be produced at any rate.
	//!
	//! Every partial is under 4kHz and every envelope is smooth, so the highest
	//! rate here and the lowest represent it identically - which is what lets a
	//! cross-rate difference be blamed on the analysis rather than on the signal.
	void synth_beats(std::vector<float> & out, unsigned rate, double seconds,
	                 double beat_bpm, int meter)
	{
		const std::size_t n = static_cast<std::size_t>(seconds * rate);
		const double beat = 60.0 / beat_bpm;
		// One partial per band, so mix_bands has something in each of the six.
		const double partials[6] = { 80.0, 300.0, 640.0, 1000.0, 2200.0, 4000.0 };
		const double weights[6]  = { 1.00, 0.45, 0.40, 0.30, 0.25, 0.18 };
		out.assign(n, 0.0f);
		for (std::size_t i = 0; i < n; i++)
		{
			const double t = static_cast<double>(i) / rate;
			const double into = std::fmod(t, beat);
			if (into >= 0.12) continue;
			// Raised cosine in, exponential out: continuous, with a continuous
			// derivative at both ends, so the spectrum decays fast.
			const double env = 0.5 * (1.0 - std::cos(2.0 * test_pi * into / 0.12))
			                   * std::exp(-12.0 * into);
			double v = 0;
			for (int b = 0; b < 6; b++)
				v += weights[b] * std::sin(2.0 * test_pi * partials[b] * t);
			const long index = static_cast<long>(t / beat);
			const double accent = (index % meter) == 0 ? 1.0 : 0.55;
			out[i] = static_cast<float>(0.3 * accent * env * v);
		}
	}

	//! Beats laid down from a tempo curve, so a track can drift or wobble by a
	//! known amount. `bpm_at` is asked for the tempo at a time in seconds and
	//! the next click placed one of its periods later, which is what a player
	//! following a tempo marking does. `synth_beats` above cannot do this: it
	//! derives beat positions from fmod, which fixes the period for the whole
	//! track.
	template<typename bpm_fn>
	void synth_beats_curve(std::vector<float> & out, unsigned rate, double seconds,
	                       int meter, bpm_fn bpm_at)
	{
		const std::size_t n = static_cast<std::size_t>(seconds * rate);
		// The same click as synth_beats, so only the beat positions differ.
		const double partials[6] = { 80.0, 300.0, 640.0, 1000.0, 2200.0, 4000.0 };
		const double weights[6]  = { 1.00, 0.45, 0.40, 0.30, 0.25, 0.18 };
		const double click = 0.12;
		out.assign(n, 0.0f);

		double at = 0.0;
		for (long index = 0; at < seconds; index++)
		{
			const double accent = (index % meter) == 0 ? 1.0 : 0.55;
			const std::size_t begin = static_cast<std::size_t>(at * rate);
			const std::size_t end =
				std::min(n, static_cast<std::size_t>((at + click) * rate) + 1);
			for (std::size_t i = begin; i < end; i++)
			{
				const double t = static_cast<double>(i) / rate;
				const double into = t - at;
				if (into < 0.0 || into >= click) continue;
				const double env = 0.5 * (1.0 - std::cos(2.0 * test_pi * into / click))
				                   * std::exp(-12.0 * into);
				double v = 0;
				for (int b = 0; b < 6; b++)
					v += weights[b] * std::sin(2.0 * test_pi * partials[b] * t);
				out[i] += static_cast<float>(0.3 * accent * env * v);
			}
			const double bpm = bpm_at(at);
			if (!(bpm > 1.0)) break;
			at += 60.0 / bpm;
		}
	}

	void synth_sine(std::vector<float> & out, unsigned rate, double seconds, double hz)
	{
		const std::size_t n = static_cast<std::size_t>(seconds * rate);
		out.assign(n, 0.0f);
		for (std::size_t i = 0; i < n; i++)
			out[i] = static_cast<float>(std::sin(2.0 * test_pi * hz * i / rate));
	}

	//! RMS of the middle half, which leaves out the filter's transient at each end.
	double middle_rms(const std::vector<float> & x)
	{
		if (x.size() < 8) return 0.0;
		const std::size_t lo = x.size() / 4, hi = x.size() - x.size() / 4;
		double sum = 0;
		for (std::size_t i = lo; i < hi; i++) sum += static_cast<double>(x[i]) * x[i];
		return std::sqrt(sum / (hi - lo));
	}

	std::vector<float> convert(unsigned from, unsigned to,
	                           const std::vector<float> & in, std::size_t block)
	{
		bpmcore::resampler rs(from, to);
		std::vector<float> out;
		if (!rs.valid() || block == 0) return out;
		out.reserve(rs.expected_output(in.size()));
		for (std::size_t at = 0; at < in.size(); at += block)
			rs.process(in.data() + at, std::min(block, in.size() - at), out);
		rs.flush(out);
		return out;
	}

	//! Checks the resampler, and the rate independence it buys.
	//!
	//! Every signal is synthesised here rather than read from disk, which is the
	//! point: this runs in CI with no audio to hand.
	int run_tempo_spread()
	{
		int failures = 0;
		int checks = 0;
		auto check = [&](bool ok, const char * what)
		{
			checks++;
			if (!ok) { std::fprintf(stderr, "tempo_spread: %s\n", what); failures++; }
		};

		const unsigned rate = 22050;
		const double seconds = 120.0;
		auto measure = [&](const std::vector<float> & mono, int threads)
		{
			bpmcore::options opt; opt.threads = threads;
			return bpmcore::analyse(mono.data(), mono.size(), rate, nullptr, &opt);
		};

		// A metronome does not fluctuate, and the figure has to say so rather
		// than report the measurement's own noise: every window should find its
		// peak at the same lag.
		std::vector<float> steady;
		synth_beats_curve(steady, rate, seconds, 4, [](double) { return 120.0; });
		const bpmcore::analysis a_steady = measure(steady, 1);
		std::printf("  steady 120        bpm %7.3f  spread %6.3f  windows %d\n",
		            a_steady.bpm, a_steady.bpm_spread, a_steady.spread_windows);
		check(a_steady.ok, "the steady track did not analyse");
		check(a_steady.spread_windows >= bpmcore::spread_min_windows,
		      "the steady track measured too few windows");
		check(a_steady.bpm_spread < 0.30, "a metronome was reported as fluctuating");
		// Nothing changes, so where the track starts is where it stays.
		check(std::fabs(a_steady.initial_bpm - a_steady.bpm) < 0.30,
		      "a metronome's opening tempo differs from its overall tempo");

		// A linear ramp is the case whose answer can be worked out in advance.
		// Only whole windows are taken, so their centres run from half a window
		// in to half a window from the end, and the tempos measured are
		// therefore uniform over that stretch of the ramp. The 10th-to-90th
		// half-span of a uniform spread is 0.4 of its width.
		const double f0 = 116.0, f1 = 124.0;
		std::vector<float> ramp;
		synth_beats_curve(ramp, rate, seconds, 4, [&](double t)
		{
			return f0 + (f1 - f0) * (t / seconds);
		});
		const bpmcore::analysis a_ramp = measure(ramp, 1);
		const double edge = (f1 - f0) * (6.0 / seconds);
		const double predicted = 0.4 * ((f1 - edge) - (f0 + edge));
		std::printf("  ramp %.0f to %.0f     bpm %7.3f  spread %6.3f  predicted %.3f\n",
		            f0, f1, a_ramp.bpm, a_ramp.bpm_spread, predicted);
		check(a_ramp.ok, "the ramped track did not analyse");
		check(std::fabs(a_ramp.bpm - 0.5 * (f0 + f1)) < 1.5,
		      "the ramp's tempo did not come out near the middle of the ramp");
		check(std::fabs(a_ramp.bpm_spread - predicted) < 0.75,
		      "the ramp's spread is not the width the geometry predicts");

		// The opening tempo is predictable for the same reason the spread is.
		// The first three windows are centred 6, 9 and 12 seconds in, so their
		// median is the ramp's value at 9 seconds - and it has to come out
		// below the whole-track figure, which is the point of reporting it.
		const double predicted_initial = f0 + (f1 - f0) * (9.0 / seconds);
		std::printf("  ramp opening      initial %7.3f  predicted %.3f\n",
		            a_ramp.initial_bpm, predicted_initial);
		check(std::fabs(a_ramp.initial_bpm - predicted_initial) < 1.0,
		      "the ramp's opening tempo is not the ramp's value there");
		check(a_ramp.initial_bpm < a_ramp.bpm - 1.0,
		      "a track that speeds up did not open below its overall tempo");

		// A wobble the windows are long enough to see has to read well clear of
		// the steady track. Deliberately not checked against a figure: a
		// 48-second cycle is only four times the window, so each window
		// averages part of it away - which is the documented limit of what this
		// measurement can see, not a defect.
		std::vector<float> wobble;
		synth_beats_curve(wobble, rate, seconds, 4, [](double t)
		{
			return 120.0 + 3.0 * std::sin(2.0 * test_pi * t / 48.0);
		});
		const bpmcore::analysis a_wobble = measure(wobble, 1);
		std::printf("  wobble 120 +/- 3  bpm %7.3f  spread %6.3f\n",
		            a_wobble.bpm, a_wobble.bpm_spread);
		check(a_wobble.ok, "the wobbling track did not analyse");
		check(a_wobble.bpm_spread > a_steady.bpm_spread + 0.5,
		      "a wobbling track did not read above a steady one");

		// Too short to say anything: two windows is not a distribution.
		std::vector<float> brief;
		synth_beats_curve(brief, rate, 15.0, 4, [](double) { return 120.0; });
		bpmcore::options one; one.threads = 1;
		const bpmcore::analysis a_brief =
			bpmcore::analyse(brief.data(), brief.size(), rate, nullptr, &one);
		std::printf("  15s               spread %6.3f  windows %d\n",
		            a_brief.bpm_spread, a_brief.spread_windows);
		check(a_brief.spread_windows < bpmcore::spread_min_windows,
		      "a 15 second track somehow measured enough windows");
		check(a_brief.bpm_spread == 0.0,
		      "a track too short to measure still reported a spread");

		// Like everything else in the analysis, the figure cannot depend on how
		// the work was divided: the windows land in reserved slots, and the
		// percentiles reduce the same values in the same order.
		const int thread_counts[] = { 2, 0 };
		for (std::size_t i = 0; i < sizeof(thread_counts) / sizeof(thread_counts[0]); i++)
		{
			const bpmcore::analysis a = measure(ramp, thread_counts[i]);
			check(a.bpm_spread == a_ramp.bpm_spread &&
			      a.spread_windows == a_ramp.spread_windows &&
			      a.initial_bpm == a_ramp.initial_bpm,
			      "the spread or opening tempo changed with the thread count");
		}

		std::printf("tempo_spread: %d checks, %d failures\n", checks, failures);
		return failures == 0 ? 0 : 1;
	}

	int run_resample()
	{
		int failures = 0;
		int checks = 0;
		auto check = [&](bool ok, const char * what)
		{
			checks++;
			if (!ok) { std::fprintf(stderr, "resample: %s\n", what); failures++; }
		};

		// Which rates reproduce the model's analysis untouched.
		const unsigned exact[] = { 11025, 22050, 44100, 88200, 176400 };
		for (std::size_t i = 0; i < sizeof(exact) / sizeof(exact[0]); i++)
			check(bpmcore::rate_matches_model(exact[i]), "an exact rate was thought inexact");
		const unsigned inexact[] = { 0, 8000, 16000, 24000, 32000, 48000, 96000, 192000, 44056 };
		for (std::size_t i = 0; i < sizeof(inexact) / sizeof(inexact[0]); i++)
			check(!bpmcore::rate_matches_model(inexact[i]), "an inexact rate was thought exact");

		// Blocking must not change a sample. Each output reads the same
		// coefficients against the same input whatever the block size, so this is
		// an equality and not a tolerance.
		std::vector<float> beats;
		synth_beats(beats, 48000, 3.0, 124.0, 4);
		const std::vector<float> whole = convert(48000, 22050, beats, beats.size());
		check(!whole.empty(), "48kHz could not be converted at all");
		const std::size_t blocks[] = { 1, 7, 577, 4096 };
		for (std::size_t i = 0; i < sizeof(blocks) / sizeof(blocks[0]); i++)
			check(convert(48000, 22050, beats, blocks[i]) == whole,
			      "streaming and one-shot conversion differ");

		// And the parallel one-shot path has to agree with both, whatever the
		// thread count: every output is an independent dot product, so dividing
		// the range must not change a bit.
		const int thread_counts[] = { 1, 2, 0 };
		for (std::size_t i = 0; i < sizeof(thread_counts) / sizeof(thread_counts[0]); i++)
		{
			bpmcore::resampler one(48000, 22050);
			std::vector<float> once;
			one.convert_all(beats.data(), beats.size(), once, thread_counts[i]);
			check(once == whole, "the parallel conversion differs from the streaming one");
		}

		// The output has to cover the input's duration, to the sample.
		const bpmcore::resampler geometry(48000, 22050);
		check(whole.size() == geometry.expected_output(beats.size()),
		      "converted length is not the expected length");
		check(std::fabs(static_cast<double>(whole.size()) / 22050.0 -
		                static_cast<double>(beats.size()) / 48000.0) <= 1.0 / 22050.0,
		      "converted duration does not match the input duration");

		// Flat to the top band edge, which is as high as the envelope reads.
		const double pass_hz[] = { 100.0, 1000.0, 5000.0, 7900.0 };
		for (std::size_t i = 0; i < sizeof(pass_hz) / sizeof(pass_hz[0]); i++)
		{
			std::vector<float> sine;
			synth_sine(sine, 48000, 2.0, pass_hz[i]);
			const double gain = middle_rms(convert(48000, 22050, sine, 8192)) / middle_rms(sine);
			std::printf("  %7.0fHz  passband gain %.5f\n", pass_hz[i], gain);
			check(std::fabs(gain - 1.0) <= 0.01, "the passband is not flat");
		}

		// An alias of anything above the stopband edge has to land far enough
		// below the signal to be invisible to a log-compressed flux.
		const double stop_hz[] = { 15000.0, 18000.0, 21000.0 };
		for (std::size_t i = 0; i < sizeof(stop_hz) / sizeof(stop_hz[0]); i++)
		{
			std::vector<float> sine;
			synth_sine(sine, 48000, 2.0, stop_hz[i]);
			const double gain = middle_rms(convert(48000, 22050, sine, 8192)) / middle_rms(sine);
			const double db = 20.0 * std::log10(gain > 1e-12 ? gain : 1e-12);
			std::printf("  %7.0fHz  alias %7.1f dB\n", stop_hz[i], db);
			check(db <= -70.0, "an alias was not rejected");
		}

		// What the whole exercise is for: one recording, six rates, one answer.
		// 48kHz used to be read through a 42.7ms window where the geometry asks
		// for 46.4ms, which moved the classifier and the metre with it.
		const unsigned rates[] = { 22050, 32000, 44100, 48000, 88200, 96000 };
		bpmcore::analysis reference;
		for (std::size_t i = 0; i < sizeof(rates) / sizeof(rates[0]); i++)
		{
			std::vector<float> audio;
			synth_beats(audio, rates[i], 40.0, 124.0, 4);
			bpmcore::options opt; opt.threads = 1;
			const bpmcore::analysis a =
				bpmcore::analyse(audio.data(), audio.size(), rates[i], nullptr, &opt);
			std::printf("  %6uHz  %7.3f BPM  %-8s p=%.3f  beat %7.3f  metre %d\n",
			            rates[i], a.bpm, bpmcore::rhythm_name(a.rhythm), a.confidence,
			            a.beat_bpm, a.meter);
			check(a.ok, "a rate failed to analyse");
			if (!a.ok) continue;
			if (i == 0) { reference = a; continue; }
			// Tighter than the 2 BPM the estimate is measured against, and
			// tighter than a tap can resolve. The point is that the input rate
			// does not enter the answer at all.
			check(std::fabs(a.bpm - reference.bpm) <= 0.05, "BPM depends on the input rate");
			check(a.rhythm == reference.rhythm, "rhythm depends on the input rate");
			check(a.meter == reference.meter, "metre depends on the input rate");
		}

		std::printf("resample: %d checks, %d failures\n", checks, failures);
		return failures == 0 ? 0 : 1;
	}

	//! One track's tuning, key and retune suggestion, written out for a person.
	//!
	//! The same fields the Python reference prints, in the same order, so the
	//! two can be read side by side while the port is being checked.
	void print_key(const bpmcore::key_analysis & k, int year)
	{
		if (!k.ok)
		{
			std::printf("  no key: too short, or no pitched content\n");
			return;
		}

		std::printf("  tuning   %+.1f cents   R=%.2f", k.tuning_cents, k.tuning_r);
		if (!k.tuning_ok) std::printf("   [low tuning confidence]");
		if (k.near_wrap) std::printf("   [near the semitone wrap - key may be a semitone out]");
		std::printf("\n");

		std::printf("  key      %-4s  (%s confidence, margin %.3f)\n",
		            bpmcore::key_name(k.best.root, k.best.minor),
		            bpmcore::key_confidence_name(k.confidence), k.margin);
		std::printf("           candidates:");
		for (int i = 0; i < k.candidate_count; i++)
			std::printf("   %s %+.3f", bpmcore::key_name(k.candidates[i].root,
			                                             k.candidates[i].minor),
			            k.candidates[i].score);
		std::printf("\n");
		if (k.major_fraction >= 0)
			std::printf("           mode over time: %.0f%% of %d windows major, "
			            "%d switch(es)\n",
			            100.0 * k.major_fraction, k.mode_windows, k.mode_switches);

		bpmcore::retune_option opts[bpmcore::key_candidate_count];
		const int n = bpmcore::suggest_retune(k.tuning_cents, year, opts,
		                                      bpmcore::key_candidate_count);
		if (n == 0)
		{
			std::printf("  retune   not suggested (year %d)\n", year);
			return;
		}
		std::printf("  retune   (year %d)\n", year);
		for (int i = 0; i < n; i++)
			std::printf("    %d. %s %.2f%%   (%+.1f c from %s)\n", i + 1,
			            opts[i].percent < 0 ? "slow down" : "speed up ",
			            std::fabs(opts[i].percent), opts[i].cents,
			            bpmcore::retune_target_name(opts[i].target));
	}

	int run_key(const char * path, int year, int threads)
	{
		std::vector<float> mono;
		std::string error;
		if (!decode_track(path, ffmpeg_path(), mono, error))
		{
			std::fprintf(stderr, "%s: %s\n", path, error.c_str());
			return 2;
		}
		bpmcore::key_analysis k;
		bpmcore::compute_key(mono.data(), mono.size(), bpmcore::odf_model_rate,
		                     k, nullptr, threads);
		std::printf("\n%s  (%.0fs)\n", path, k.duration);
		print_key(k, year);
		return k.ok ? 0 : 3;
	}

	//! Every track in a list file, one row each, for scoring a whole
	//! collection against its discography data.
	//!
	//! The list may carry a year after a tab, which is what the retune
	//! suggestion needs and what no amount of signal processing can supply.
	int run_key_batch(const char * list_path, int threads)
	{
		std::ifstream in(list_path, std::ios::binary);
		if (!in)
		{
			std::fprintf(stderr, "cannot open %s\n", list_path);
			return 2;
		}

		std::vector<std::pair<std::string, int> > items;
		std::string line;
		while (std::getline(in, line))
		{
			if (!line.empty() && line.back() == '\r') line.pop_back();
			if (items.empty() && line.compare(0, 3, "\xEF\xBB\xBF") == 0) line.erase(0, 3);
			if (line.empty() || line[0] == '#') continue;
			const std::size_t tab = line.find('\t');
			if (tab == std::string::npos) items.push_back(std::make_pair(line, 0));
			else items.push_back(std::make_pair(line.substr(0, tab),
			                                    std::atoi(line.c_str() + tab + 1)));
		}
		if (items.empty())
		{
			std::fprintf(stderr, "%s lists no files\n", list_path);
			return 2;
		}

		const std::string ffmpeg = ffmpeg_path();
		std::fprintf(stderr, "%d tracks, %s precision, ffmpeg %s\n",
		             static_cast<int>(items.size()), BPMCORE_TEST_SCALAR, ffmpeg.c_str());

		std::printf("#path\tok\ttuning\tr\tkey\tcand1\tscore1\tcand2\tscore2\tcand3\tscore3"
		            "\tmargin\tconfidence\tmajor_frac\tswitches\twindows\tyear\tretune\n");

		int failed = 0;
		std::vector<float> mono;
		for (std::size_t i = 0; i < items.size(); i++)
		{
			std::string error;
			bpmcore::key_analysis k;
			if (!decode_track(items[i].first, ffmpeg, mono, error))
			{
				std::fprintf(stderr, "%s: %s\n", items[i].first.c_str(), error.c_str());
				failed++;
			}
			else
			{
				bpmcore::compute_key(mono.data(), mono.size(), bpmcore::odf_model_rate,
				                     k, nullptr, threads);
				if (!k.ok) failed++;
			}

			bpmcore::retune_option opts[bpmcore::key_candidate_count];
			const int n = k.ok ? bpmcore::suggest_retune(k.tuning_cents, items[i].second,
			                                             opts, bpmcore::key_candidate_count)
			                   : 0;
			std::string retune;
			for (int j = 0; j < n; j++)
			{
				char buf[64];
				std::snprintf(buf, sizeof buf, "%s%+.2f%%@%s", j ? " " : "",
				              opts[j].percent, bpmcore::retune_target_name(opts[j].target));
				retune += buf;
			}

			std::printf("%s\t%d\t%.2f\t%.3f\t%s", items[i].first.c_str(), k.ok ? 1 : 0,
			            k.tuning_cents, k.tuning_r,
			            k.ok ? bpmcore::key_name(k.best.root, k.best.minor) : "-");
			for (int j = 0; j < bpmcore::key_candidate_count; j++)
				std::printf("\t%s\t%.4f",
				            j < k.candidate_count
				                ? bpmcore::key_name(k.candidates[j].root, k.candidates[j].minor)
				                : "-",
				            j < k.candidate_count ? k.candidates[j].score : 0.0);
			std::printf("\t%.4f\t%s\t%.3f\t%d\t%d\t%d\t%s\n", k.margin,
			            bpmcore::key_confidence_name(k.confidence),
			            k.major_fraction, k.mode_switches, k.mode_windows,
			            items[i].second, retune.c_str());
			std::fflush(stdout);
			if ((i + 1) % 25 == 0 || i + 1 == items.size())
				std::fprintf(stderr, "\r%d/%d", static_cast<int>(i + 1),
				             static_cast<int>(items.size()));
		}
		std::fprintf(stderr, "\n%d of %d tracks produced no key\n",
		             failed, static_cast<int>(items.size()));
		return 0;
	}

	//! A triad progression at a known detune, for the key self-test.
	//!
	//! Three partials a note, so the spectrum has something for the whitening
	//! to flatten and for the peak picker to find above it, and a little noise
	//! so the running median is measuring a background rather than zero.
	void synth_progression(std::vector<float> & out, unsigned rate, double cents,
	                       int transpose, const int * roots, int chords,
	                       double chord_seconds)
	{
		const std::size_t n = static_cast<std::size_t>(chords * chord_seconds * rate);
		out.assign(n, 0.0f);
		const double detune = std::pow(2.0, (cents + 100.0 * transpose) / 1200.0);
		// A deterministic hiss: a test that fails one run in twenty is worse
		// than no test.
		std::uint32_t seed = 12345;
		for (int c = 0; c < chords; c++)
		{
			const std::size_t begin = static_cast<std::size_t>(c * chord_seconds * rate);
			const std::size_t end = std::min(n, static_cast<std::size_t>(
				(c + 1) * chord_seconds * rate));
			// Major triad on the root, in the octave above middle C.
			const int semis[3] = { roots[c], roots[c] + 4, roots[c] + 7 };
			for (int v = 0; v < 3; v++)
			{
				// Pitch class `semis[v]` as a frequency, C4 = 261.6Hz.
				const double f0 = 261.625565 * std::pow(2.0, semis[v] / 12.0) * detune;
				for (int h = 1; h <= 3; h++)
				{
					const double f = f0 * h;
					if (f > 0.45 * rate) break;
					const double amp = 0.20 / h;
					for (std::size_t i = begin; i < end; i++)
						out[i] += static_cast<float>(
							amp * std::sin(2.0 * test_pi * f * (i - begin) / rate));
				}
			}
			for (std::size_t i = begin; i < end; i++)
			{
				seed = seed * 1664525u + 1013904223u;
				out[i] += static_cast<float>(
					1e-3 * (static_cast<double>(seed >> 8) / 8388608.0 - 1.0));
			}
		}
	}

	//! Tuning, key and retune, against signals whose answers are known.
	//!
	//! Synthesised here rather than read from disk, so this runs in CI with no
	//! audio to hand. It cannot say whether the detector is any good on real
	//! recordings - only the labelled collections can, and what they say is in
	//! key-detection-feature-plan.md. What it checks is that the arithmetic
	//! holds: that the offset comes back, that taking it out leaves the key
	//! where it was, and that transposing the audio transposes the answer.
	int run_key_synth()
	{
		int failures = 0, checks = 0;
		auto check = [&](bool ok, const char * what)
		{
			checks++;
			if (!ok) { std::fprintf(stderr, "key_synth: %s\n", what); failures++; }
		};

		// I-IV-V-I in C, twice round, with the relative minor for colour.
		static const int progression[] = { 0, 5, 7, 0, 9, 5, 7, 0 };
		const int chords = static_cast<int>(sizeof progression / sizeof *progression);

		std::vector<float> x;
		bpmcore::key_analysis k;

		// The offset comes back, at every rate the analysis might see and at
		// both ends of the range that matters - A=435 is -19.79 cents.
		static const double detunes[] = { 0.0, -19.79, +31.0, -44.0 };
		static const unsigned rates[] = { 22050, 44100 };
		for (unsigned r = 0; r < sizeof rates / sizeof *rates; r++)
		{
			for (unsigned d = 0; d < sizeof detunes / sizeof *detunes; d++)
			{
				synth_progression(x, rates[r], detunes[d], 0, progression, chords, 4.0);
				bpmcore::compute_key(x.data(), x.size(), rates[r], k, nullptr, 1);
				if (!k.ok) { check(false, "synth produced no analysis"); continue; }
				const double err = k.tuning_cents - detunes[d];
				if (std::fabs(err) > 2.0)
					std::fprintf(stderr, "key_synth: %uHz detune %+.2f read %+.2f\n",
					             rates[r], detunes[d], k.tuning_cents);
				check(std::fabs(err) <= 2.0, "tuning offset off by more than 2 cents");
				check(k.tuning_r > 0.8, "tuning confidence low on a synthetic tone");
			}
		}

		// Taking the offset out before binning is the step the whole thing
		// rests on: the same music at three speeds has to give one key.
		int root0 = -1;
		bool minor0 = false;
		for (unsigned d = 0; d < sizeof detunes / sizeof *detunes; d++)
		{
			synth_progression(x, 22050, detunes[d], 0, progression, chords, 4.0);
			bpmcore::compute_key(x.data(), x.size(), 22050, k, nullptr, 1);
			if (!k.ok) { check(false, "synth produced no key"); continue; }
			if (d == 0) { root0 = k.best.root; minor0 = k.best.minor; continue; }
			if (k.best.root != root0 || k.best.minor != minor0)
				std::fprintf(stderr, "key_synth: detune %+.2f moved the key from %s to %s\n",
				             detunes[d], bpmcore::key_name(root0, minor0),
				             bpmcore::key_name(k.best.root, k.best.minor));
			check(k.best.root == root0 && k.best.minor == minor0,
			      "detuning the audio changed the key");
		}
		// A major-key progression should read major, and on C.
		check(root0 == 0 && !minor0, "I-IV-V-I in C did not read as C major");

		// And transposing the audio has to transpose the answer, which is what
		// catches a pitch-class table that is rotated or reflected.
		for (int t = 1; t <= 11; t++)
		{
			synth_progression(x, 22050, 0.0, t, progression, chords, 4.0);
			bpmcore::compute_key(x.data(), x.size(), 22050, k, nullptr, 1);
			if (!k.ok) { check(false, "transposed synth produced no key"); continue; }
			const int want = (root0 + t) % 12;
			if (k.best.root != want || k.best.minor != minor0)
				std::fprintf(stderr, "key_synth: +%d semitones read %s, wanted %s\n",
				             t, bpmcore::key_name(k.best.root, k.best.minor),
				             bpmcore::key_name(want, minor0));
			check(k.best.root == want && k.best.minor == minor0,
			      "transposing the audio did not transpose the key");
		}

		// Threading cannot move the answer; the batch sweeps depend on it.
		synth_progression(x, 22050, -19.79, 3, progression, chords, 4.0);
		bpmcore::key_analysis one, many;
		bpmcore::compute_key(x.data(), x.size(), 22050, one, nullptr, 1);
		bpmcore::compute_key(x.data(), x.size(), 22050, many, nullptr, 4);
		check(one.ok && many.ok && one.best.root == many.best.root
		      && one.best.minor == many.best.minor
		      && std::fabs(one.tuning_cents - many.tuning_cents) < 1e-9,
		      "one thread and four gave different answers");

		// Keys are spelt by their signature. Bb, not A#.
		check(std::strcmp(bpmcore::key_name(10, false), "Bb") == 0, "Bb printed as A#");
		check(std::strcmp(bpmcore::key_name(3, false), "Eb") == 0, "Eb printed as D#");
		check(std::strcmp(bpmcore::key_name(7, true), "Gm") == 0, "Gm misspelt");
		check(std::strcmp(bpmcore::key_name(0, true), "Cm") == 0, "Cm misspelt");

		// A=435 is 19.79 cents flat, and correcting it means playing faster.
		check(std::fabs(bpmcore::key_a435_offset() + 19.79) < 0.01, "A=435 is not -19.79c");
		check(bpmcore::retune_percent(-19.79) > 1.14
		      && bpmcore::retune_percent(-19.79) < 1.16,
		      "correcting a flat transfer should speed it up about 1.15%");
		check(std::fabs(bpmcore::retune_percent(0.0)) < 1e-12, "no offset, no correction");

		// The era window, and what falls outside it.
		bpmcore::retune_option opt[3];
		check(bpmcore::suggest_retune(-19.8, 0, opt, 3) == 0, "no year, no suggestion");
		check(bpmcore::suggest_retune(-19.8, 1976, opt, 3) == 0, "1976 is past the window");
		check(bpmcore::suggest_retune(-19.8, 1975, opt, 3) > 0, "1975 is inside it");

		// A 1938 side 19.8 cents flat is simply at A=435 and needs nothing.
		int n = bpmcore::suggest_retune(-19.8, 1938, opt, 3);
		check(n >= 1 && opt[0].target == bpmcore::retune_a435
		      && std::fabs(opt[0].cents) < 1.0,
		      "a 1938 side at A=435 should be left alone");
		// The same measurement in 1950, when nobody was cutting at 435 any
		// more, is a transfer running slow and wants speeding up.
		n = bpmcore::suggest_retune(-19.8, 1950, opt, 3);
		check(n >= 1 && opt[0].target == bpmcore::retune_a440 && opt[0].percent > 1.0,
		      "a 1950 side 20 cents flat should be sped up to A=440");
		// In the transition years both are offered, nearest first.
		n = bpmcore::suggest_retune(-2.0, 1942, opt, 3);
		check(n >= 2 && opt[0].target == bpmcore::retune_a440,
		      "1942 at pitch should read as A=440 first, with A=435 behind it");
		check(bpmcore::key_era_p435(1938) > 0.5 && bpmcore::key_era_p435(1944) < 0.5
		      && bpmcore::key_era_p435(1950) == 0.0,
		      "the era prior does not fall across the transition");

		std::printf("key_synth: %d checks, %d failures\n", checks, failures);
		return failures == 0 ? 0 : 1;
	}

	int run_model(const char * path)
	{
		std::ifstream in(path);
		if (!in)
		{
			std::fprintf(stderr, "cannot open %s\n", path);
			return 2;
		}

		int checked = 0, class_mismatch = 0;
		double worst_prob = 0;
		std::string line;
		while (std::getline(in, line))
		{
			if (line.empty()) continue;
			// path \t true class \t expected class \t expected probability \t features
			std::vector<std::string> fields;
			std::string field;
			std::istringstream ls(line);
			while (std::getline(ls, field, '\t')) fields.push_back(field);
			if (fields.size() < 5) continue;

			const std::string expect_class = fields[2];
			const double expect_prob = std::atof(fields[3].c_str());

			std::vector<double> features;
			std::istringstream fs(fields[4]);
			double v;
			while (fs >> v) features.push_back(v);
			if (static_cast<int>(features.size()) != bpmcore::feature_count)
			{
				std::fprintf(stderr, "case has %d features, expected %d\n",
				             static_cast<int>(features.size()),
				             (int)bpmcore::feature_count);
				return 5;
			}

			double confidence = 0;
			const int cls = bpmcore::classify(features, &confidence);
			// The trainer writes class names in lower case.
			const std::string got = bpmcore::rhythm_name(cls);
			if (lower(got) != lower(expect_class))
			{
				class_mismatch++;
				std::fprintf(stderr, "class mismatch: got %s expected %s\n",
				             got.c_str(), expect_class.c_str());
			}
			worst_prob = std::max(worst_prob, std::fabs(confidence - expect_prob));
			checked++;
		}

		std::printf("model: %d cases, %d class mismatches, worst probability delta %.3e\n",
		            checked, class_mismatch, worst_prob);
		if (checked == 0) return 6;
		// The trees are exported verbatim, so anything above float noise is a bug.
		return (class_mismatch == 0 && worst_prob < 1e-5) ? 0 : 1;
	}
}

int main(int argc, char ** argv)
{
	const std::string mode = argc >= 2 ? argv[1] : "";
	if (mode == "model" && argc >= 3) return run_model(argv[2]);
	if (mode == "resample") return run_resample();
	if (mode == "tempo_spread") return run_tempo_spread();
	if (mode == "trajectory" && argc >= 4)
		return run_trajectory(argv[2], static_cast<unsigned>(std::atoi(argv[3])));
	if (mode == "pipeline" && argc >= 4)
		return run_pipeline(argv[2], static_cast<unsigned>(std::atoi(argv[3])),
		                    argc >= 5 ? std::atoi(argv[4]) : 1);
	if (mode == "profile" && argc >= 4)
		return run_profile(argv[2], static_cast<unsigned>(std::atoi(argv[3])),
		                   argc >= 5 ? std::max(1, std::atoi(argv[4])) : 3,
		                   argc >= 6 ? std::atoi(argv[5]) : 1);
	if (mode == "fft_sizes") return run_fft_sizes();
	if (mode == "key_synth") return run_key_synth();
	if (mode == "key" && argc >= 3)
		return run_key(argv[2], argc >= 4 ? std::atoi(argv[3]) : 0,
		               argc >= 5 ? std::atoi(argv[4]) : 0);
	if (mode == "key_batch" && argc >= 3)
		return run_key_batch(argv[2], argc >= 4 ? std::atoi(argv[3]) : 0);
	if (mode == "batch" && argc >= 3)
		return run_batch(argv[2], argc >= 4 ? std::atoi(argv[3]) : 0);
	if (mode == "bench" && argc >= 4)
		return run_bench(argv[2], static_cast<unsigned>(std::atoi(argv[3])),
		                 argc >= 5 ? std::max(1, std::atoi(argv[4])) : 3,
		                 argc >= 6 ? std::atoi(argv[5]) : 0,
		                 argc >= 7 ? std::atoi(argv[6]) != 0 : true);

	std::fprintf(stderr,
		"usage: bpmcore_test model <cases file>\n"
		"       bpmcore_test resample\n"
		"       bpmcore_test tempo_spread\n"
		"       bpmcore_test pipeline <raw f32 mono file> <sample rate>\n"
		"       bpmcore_test fft_sizes\n"
		"       bpmcore_test key_synth\n"
		"       bpmcore_test key <audio file> [year] [threads]\n"
		"       bpmcore_test key_batch <list of audio files, tab, year> [threads]\n"
		"       bpmcore_test batch <list of audio files> [threads]\n"
		"       bpmcore_test bench <raw f32 mono file> <sample rate> [repeats] [threads] [key 0|1]\n"
		"       bpmcore_test trajectory <raw f32 mono file> <sample rate>\n");
	return 64;
}
