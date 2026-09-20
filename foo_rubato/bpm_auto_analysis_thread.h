#ifndef __BPM_AUTO_ANALYSIS_THREAD_H__
#define __BPM_AUTO_ANALYSIS_THREAD_H__

#include <vector>

#include <SDK/foobar2000.h>

#include "bpm_track_result.h"

class bpm_auto_analysis_thread : public threaded_process_callback
{
	public:
		bpm_auto_analysis_thread(metadb_handle_list_cref p_tracks);
		void start();

	private:
		void run(threaded_process_status & p_status, abort_callback & p_abort) override;
		void on_done(ctx_t p_wnd, bool p_was_aborted) override;

		pfc::list_t<metadb_handle_ptr> m_tracks;
		pfc::list_t<file_info_impl> m_infos;
		//! What each surviving track came back with, in the order the results
		//! window shows them. The tempo figures are kept as numbers rather
		//! than formatted here, so that doubling or halving a result in that
		//! window scales the fluctuation and the opening tempo with it.
		std::vector<bpm_track_result> m_results;
};

#endif // __BPM_AUTO_ANALYSIS_THREAD_H__
