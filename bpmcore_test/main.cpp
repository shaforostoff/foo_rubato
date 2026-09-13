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
//   batch <list of audio files> [threads]
//       decode and analyse every track in a list file, one TSV row each, so
//       two builds of the analysis can be compared over a whole collection.
//       This is the only mode that needs ffmpeg, and the only one that reads
//       anything but raw PCM.
//
//   bench <raw f32 mono file> <sample rate> [repeats]
//       time the analysis.
//
//   trajectory <raw f32 mono file> <sample rate>
//       print the tempo measured in each autocorrelation window, which is what
//       the reported spread is the 10th-to-90th half-span of. A real drift
//       walks; a track the windows cannot track scatters.

#include <bpmcore/bpmcore.h>
// The harness reaches past the public interface for the classifier check, so
// that the feature count and the tree walk are not restated here.
#include <bpmcore/internal.h>

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

	int run_bench(const char * path, unsigned sample_rate, int repeats, int threads)
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
			bpmcore::options opt; opt.threads = threads;
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
	if (mode == "batch" && argc >= 3)
		return run_batch(argv[2], argc >= 4 ? std::atoi(argv[3]) : 0);
	if (mode == "bench" && argc >= 4)
		return run_bench(argv[2], static_cast<unsigned>(std::atoi(argv[3])),
		                 argc >= 5 ? std::max(1, std::atoi(argv[4])) : 3,
		                 argc >= 6 ? std::atoi(argv[5]) : 0);

	std::fprintf(stderr,
		"usage: bpmcore_test model <cases file>\n"
		"       bpmcore_test resample\n"
		"       bpmcore_test tempo_spread\n"
		"       bpmcore_test pipeline <raw f32 mono file> <sample rate>\n"
		"       bpmcore_test batch <list of audio files> [threads]\n"
		"       bpmcore_test bench <raw f32 mono file> <sample rate> [repeats]\n"
		"       bpmcore_test trajectory <raw f32 mono file> <sample rate>\n");
	return 64;
}
