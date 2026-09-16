#include "stdafx.h"
#include "bpm_auto_analysis_thread.h"
#include "bpm_analysis.h"
#include "preferences.h"
#include "bpm_ui.h"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>

#ifndef _WIN32
// The MB_* and ID* constants, which off Windows the SDK defines itself, and
// the thread QoS classes.
#include <SDK/messageBox.h>
#include <pthread.h>
#endif

namespace
{
	//! A modal message box with the buttons MB_YESNO and friends name.
	//!
	//! Not fb2k::messageBox on both platforms, though it is the SDK's own
	//! cross-platform drop-in and would save this function. It reaches the
	//! screen through popup_message_v3, which the SDK marks "since 1.5", and
	//! the 32 bit build declares itself loadable by foobar2000 1.2 and later -
	//! FOOBAR2000_TARGET_VERSION_COMPATIBLE is 72 there. uMessageBox has no
	//! such floor and is what this component has always called, so on Windows
	//! it keeps calling it; a port is not the place to move the oldest
	//! foobar2000 this runs on. uMessageBox is Win32 only - it takes an HWND
	//! and lives in shared.dll's exports - which is why macOS needs the other
	//! one.
	int bpm_message_box(const char * text, unsigned type)
	{
#ifdef _WIN32
		return uMessageBox(core_api::get_main_window(), text, "Rubato BPM Analyzer", type);
#else
		return fb2k::messageBox(core_api::get_main_window(), text, "Rubato BPM Analyzer", type);
#endif
	}
}

/***** Threading *****/
//
// Tracks are scanned several at a time. Decoding one track is a solid block of
// one core and dwarfs the analysis that follows it, so reading more than one
// track at once is the only thing that makes a library scan faster - see
// docs/tango-analysis.md for the measurements.
//
// Two kinds of thread run here. Several scanning threads take tracks off a
// shared counter; the thread threaded_process gave us does not scan at all,
// and owns the progress dialog instead. That division is not tidiness:
// threaded_process_status is handed to one worker thread and nothing in the
// SDK promises it is safe from several at once, so exactly one thread ever
// touches it.

bpm_auto_analysis_thread::bpm_auto_analysis_thread(metadb_handle_list_cref p_tracks)
{
	m_tracks.add_items(p_tracks);
	m_infos.set_size(m_tracks.get_count());
}

void bpm_auto_analysis_thread::start()
{
	// A track whose info foobar2000 has not read yet cannot be checked for a
	// BPM tag and has no title to show, so it goes - but it says so. Dropping
	// tracks the user selected without a word is how a selection of twenty
	// comes back as one row.
	{
		bit_array_bittable mask(m_tracks.get_count());

		// For each item in the playlist selection
		for (t_size index = 0; index < m_tracks.get_count(); index++)
		{
			const bool have_info = m_tracks[index]->get_info(m_infos[index]);

			if (!have_info)
			{
				FB2K_console_formatter() << "foo_rubato: no info read yet for "
				                         << m_tracks[index]->get_path()
				                         << ", not analysing it";
			}

			mask.set(index, !have_info);
		}

		m_tracks.remove_mask(mask);
		m_infos.remove_mask(mask);
	}

	if (m_tracks.get_count() == 0) return;

	// Everything selected is analysed. It used to be that one selected track
	// without a BPM tag made every track that had one disappear, silently, so
	// asking for twenty could return a single row; it also left the "BPM from
	// tag" column - which is there so a measurement can be read against the
	// tap beside it - impossible to fill in exactly that case. Analysing
	// writes nothing to the files on its own: the results window is where that
	// is decided.
	//
	// Unless the results window is skipped. With "write tags automatically" on
	// the numbers go straight to the files, so re-analysing a track overwrites
	// whatever its BPM tag held - a hand tap included - with nothing shown
	// first. That is the one case worth asking about.
	if (bpm_config_auto_write_tag)
	{
		const pfc::string8 bpm_tag = bpm_tag_name();
		t_size tagged = 0;

		for (t_size index = 0; index < m_infos.get_size(); index++)
		{
			if (m_infos[index].meta_exists(bpm_tag)) tagged++;
		}

		const t_size total = m_tracks.get_count();

		if (tagged == total)
		{
			pfc::string_formatter message;
			message << (total == 1 ? "The selected track already has a "
			                       : "All of the selected tracks already have a ")
			        << bpm_tag.get_ptr() << " tag, and \"write tags "
			        << "automatically\" is on - so analysing "
			        << (total == 1 ? "it" : "them")
			        << " overwrites what the tag holds without showing you the "
			        << "results first.\n\nAnalyse anyway?";

			if (bpm_message_box(message.get_ptr(), MB_YESNO | MB_ICONQUESTION) != IDYES)
			{
				return;
			}
		}
		else if (tagged > 0)
		{
			pfc::string_formatter message;
			message << tagged << " of the " << total << " selected tracks already have a "
			        << bpm_tag.get_ptr() << " tag, and \"write tags "
			        << "automatically\" is on - so analysing them overwrites what "
			        << "those tags hold without showing you the results first.\n\n"
			        << "Yes - analyse all " << total << "\n"
			        << "No - analyse only the " << (total - tagged) << " with no "
			        << bpm_tag.get_ptr() << " tag\n"
			        << "Cancel - analyse nothing";

			const int response = bpm_message_box(message.get_ptr(),
			                                     MB_YESNOCANCEL | MB_ICONQUESTION);

			if (response != IDYES && response != IDNO) return;

			if (response == IDNO)
			{
				bit_array_bittable mask(total);

				for (t_size index = 0; index < total; index++)
				{
					mask.set(index, m_infos[index].meta_exists(bpm_tag));
				}

				m_tracks.remove_mask(mask);
				m_infos.remove_mask(mask);
			}
		}
	}

	threaded_process::g_run_modeless( 
		this,
		threaded_process::flag_show_abort | 
		threaded_process::flag_show_delayed |
		threaded_process::flag_show_minimize |
		threaded_process::flag_show_progress_dual |
		threaded_process::flag_show_item,
		core_api::get_main_window(),
		"Analysing BPMs..."
		);
}

namespace
{
	//! Ask for this thread to be scheduled below the ones that matter.
	//!
	//! A core count is only half of leaving room for playback. If the machine
	//! ends up oversubscribed anyway, the scan is what should wait: playback
	//! skipping is unforgivable, and a scan finishing a few seconds later is
	//! not.
	void deprioritise_this_thread()
	{
#ifdef _WIN32
		SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
#else
		// QOS_CLASS_UTILITY is macOS's name for this exact case: long running
		// work nobody is waiting on, which the scheduler is free to put on the
		// efficiency cores and will hold behind anything interactive.
		pthread_set_qos_class_self_np(QOS_CLASS_UTILITY, 0);
#endif
	}

	//! Tracks scanned at once.
	//!
	//! Two cores short of the machine, so that starting a scan in the middle
	//! of a set does not take the core the audio thread needs: every scanning
	//! thread holds a decoder flat out, and foobar2000's own playback decode
	//! and its user interface want one between them. Clamped to a single scan
	//! where there are not the cores to give any away.
	int scan_workers()
	{
		// hardware_concurrency is allowed to answer that it does not know.
		const int cores = static_cast<int>(std::thread::hardware_concurrency());
		if (cores <= 0) return 1;
		return std::max(1, cores - 2);
	}

	//! What the scanning threads and the thread driving the dialog share.
	//! Every field is read and written under `lock`.
	struct scan_state
	{
		std::mutex lock;
		std::condition_variable changed;

		t_size next = 0;       //!< the next track nobody has claimed
		t_size finished = 0;   //!< tracks whose analysis has returned
		int running = 0;       //!< scanning threads still alive

		//! Per scanning thread: which track it is holding, SIZE_MAX between
		//! tracks, and how far through that track it is.
		std::vector<t_size> holding;
		std::vector<double> fraction;
	};

	//! One of these per scanning thread. It publishes into scan_state rather
	//! than touching the progress dialog.
	class worker_progress : public bpm_analysis_progress
	{
	public:
		worker_progress(scan_state & state, int slot) : m_state(state), m_slot(slot) {}

		void fraction(double f) override
		{
			// Deliberately no notify. The dialog is refreshed on a timer, and
			// bpmcore reports progress often enough that waking the other
			// thread every time would cost more than it tells anyone.
			std::lock_guard<std::mutex> guard(m_state.lock);
			m_state.fraction[m_slot] = f;
		}

	private:
		scan_state & m_state;
		int m_slot;
	};

	//! One scanning thread: take the next unclaimed track, analyse it, repeat
	//! until the list runs out or the user aborts.
	//!
	//! Results are written at the track's own index, so they stay in the order
	//! the user selected however the scans interleave, and no two threads ever
	//! touch the same element.
	//!
	//! Nothing may escape this function. An exception leaving a std::thread
	//! calls std::terminate, which would take foobar2000 down with it.
	void scan_thread(scan_state & state, int slot, const metadb_handle_list & tracks,
	                 std::vector<bpmcore::analysis> & results, std::vector<char> & missing,
	                 int analysis_threads, abort_callback & abort)
	{
		deprioritise_this_thread();

		worker_progress progress(state, slot);
		const t_size total = tracks.get_count();

		for (;;)
		{
			if (abort.is_aborting()) break;

			t_size index;
			{
				std::lock_guard<std::mutex> guard(state.lock);
				if (state.next >= total) break;
				index = state.next++;
				state.holding[slot] = index;
				state.fraction[slot] = 0.0;
			}

			try
			{
				// A file can go missing between being selected and being
				// reached, and then produces no row rather than an empty one.
				if (!filesystem::g_exists(tracks[index]->get_path(), abort))
				{
					missing[index] = 1;
				}
				else
				{
					results[index] = bpm_analyse(tracks[index], progress, abort, analysis_threads);
				}
			}
			catch (const exception_aborted &)
			{
				// Nothing is shown after an abort, so the half-finished row
				// can stand as it is.
			}
			catch (const std::exception & exc)
			{
				FB2K_console_formatter() << "foo_rubato: error analysing "
				                         << tracks[index]->get_path() << ": " << exc;
			}
			catch (...)
			{
				FB2K_console_formatter() << "foo_rubato: unknown error analysing "
				                         << tracks[index]->get_path();
			}

			{
				std::lock_guard<std::mutex> guard(state.lock);
				state.holding[slot] = SIZE_MAX;
				state.fraction[slot] = 0.0;
				state.finished++;
			}
			state.changed.notify_all();
		}

		{
			std::lock_guard<std::mutex> guard(state.lock);
			state.holding[slot] = SIZE_MAX;
			state.running--;
		}
		state.changed.notify_all();
	}
}

void bpm_auto_analysis_thread::run(threaded_process_status & p_status, abort_callback & p_abort)
{
	const t_size total = m_tracks.get_count();
	if (total == 0) return;

	std::vector<bpmcore::analysis> results(total);
	std::vector<char> missing(total, 0);

	int workers = scan_workers();
	if (static_cast<t_size>(workers) > total) workers = static_cast<int>(total);

	// Asked to decide for itself, bpmcore spreads its spectral stage across
	// the whole machine. With whole tracks already running side by side that
	// would be the same cores counted twice, so each scan keeps its analysis
	// on its own thread; the two cores held back are held back, not handed out
	// here. A lone track has nothing to share with and gets the machine.
	const int analysis_threads = workers > 1 ? 1 : 0;

	scan_state state;
	state.holding.assign(static_cast<std::size_t>(workers), SIZE_MAX);
	state.fraction.assign(static_cast<std::size_t>(workers), 0.0);
	state.running = workers;

	p_status.set_progress(0, total);
	p_status.set_progress_secondary(0, 1000);

	std::vector<std::thread> pool;
	pool.reserve(static_cast<std::size_t>(workers));
	for (int slot = 0; slot < workers; slot++)
	{
		try
		{
			pool.push_back(std::thread([&, slot]()
			{
				scan_thread(state, slot, m_tracks, results, missing, analysis_threads, p_abort);
			}));
		}
		catch (const std::exception & exc)
		{
			// The machine would not give us the thread. Carry on with the ones
			// it did give us - and on no account let this leave the function,
			// because a std::thread destroyed while still joinable calls
			// std::terminate and would take foobar2000 with it.
			FB2K_console_formatter() << "foo_rubato: could not start scanning thread "
			                         << (slot + 1) << " of " << workers << ": " << exc;
			std::lock_guard<std::mutex> guard(state.lock);
			state.running -= workers - slot;
			break;
		}
	}

	// This thread scans nothing. It owns the dialog, which is what makes every
	// other thread's silence about the dialog safe.
	for (;;)
	{
		metadb_handle_list in_flight;
		t_size done = 0;
		double secondary = 0;
		bool live = true;

		{
			std::unique_lock<std::mutex> guard(state.lock);
			state.changed.wait_for(guard, std::chrono::milliseconds(100),
			                       [&state]() { return state.running == 0; });

			live = state.running > 0;
			done = state.finished;

			double sum = 0;
			for (int slot = 0; slot < workers; slot++)
			{
				if (state.holding[slot] == SIZE_MAX) continue;
				in_flight.add_item(m_tracks[state.holding[slot]]);
				sum += state.fraction[slot];
			}

			// The average of the tracks in flight, which is the only reading
			// of a single secondary bar that means anything once there is
			// more than one track under it.
			if (in_flight.get_count() > 0) secondary = sum / in_flight.get_count();
		}

		p_status.set_progress(done, total);
		p_status.set_progress_secondary(static_cast<t_size>(secondary * 1000.0), 1000);
		if (in_flight.get_count() == 1)
		{
			p_status.set_item_path(in_flight[0]->get_location().get_path());
		}
		else if (in_flight.get_count() > 1)
		{
			// Renders as "a.flac, b.flac and 4 more". The SDK has a helper for
			// exactly this case, several items being worked on at once.
			p_status.set_items(in_flight);
		}

		if (!live) break;
	}

	for (std::size_t t = 0; t < pool.size(); t++) pool[t].join();

	// Not one thread could be started, so this one does the scanning after
	// all, with the dialog left where it stands: a scan without a moving bar
	// beats no scan.
	if (pool.empty() && !p_abort.is_aborting())
	{
		{
			std::lock_guard<std::mutex> guard(state.lock);
			state.running = 1;
		}
		scan_thread(state, 0, m_tracks, results, missing, 0, p_abort);
	}

	// An abort is reported by throwing, as it was when this ran one track at a
	// time; on_done then puts up no results window.
	p_abort.check();

	{
		bit_array_bittable mask(total);
		for (t_size index = 0; index < total; index++) mask.set(index, missing[index] != 0);
		m_tracks.remove_mask(mask);
		m_infos.remove_mask(mask);
	}

	m_bpm_results.clear();
	m_rhythms.clear();
	m_spreads.clear();
	m_initial_bpms.clear();

	for (t_size index = 0; index < total; index++)
	{
		if (missing[index] != 0) continue;

		const bpmcore::analysis & result = results[index];
		m_bpm_results.push_back(result.bpm);
		m_rhythms.push_back(result.ok ? bpmcore::rhythm_name(result.rhythm) : "");
		m_spreads.push_back(result.ok ? result.bpm_spread : 0.0);
		m_initial_bpms.push_back(result.ok ? result.initial_bpm : 0.0);
	}
}

void bpm_auto_analysis_thread::on_done(ctx_t p_wnd, bool p_was_aborted)
{

	if (!p_was_aborted && core_api::assert_main_thread())
	{
		bpm_show_results(m_tracks, m_infos, m_bpm_results, m_rhythms, m_spreads,
		                 m_initial_bpms);
	}
}
