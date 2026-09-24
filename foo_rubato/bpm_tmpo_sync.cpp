#include "stdafx.h"

#include "bpm_tmpo_sync.h"

#include "bpm_tag_fields.h"
#include "mp4_tmpo.h"

#include <cmath>
#include <cstdlib>
#include <cstring>

namespace
{
	//! mp4_io over a foobar2000 file, so the write goes through the same
	//! filesystem layer - and the same path schemes - that tagged the file.
	class fb2k_mp4_io : public mp4_io
	{
	public:
		explicit fb2k_mp4_io(file::ptr f) : m_file(f) {}

		std::uint64_t size() override
		{
			try { return m_file->get_size_ex(fb2k::noAbort); }
			catch (...) { return 0; }
		}

		bool read(std::uint64_t offset, void * buffer, std::size_t bytes) override
		{
			try
			{
				m_file->seek(offset, fb2k::noAbort);
				m_file->read_object(buffer, bytes, fb2k::noAbort);
				return true;
			}
			catch (...) { return false; }
		}

		bool write(std::uint64_t offset, const void * buffer, std::size_t bytes) override
		{
			try
			{
				m_file->seek(offset, fb2k::noAbort);
				m_file->write_object(buffer, bytes, fb2k::noAbort);
				return true;
			}
			catch (...) { return false; }
		}

	private:
		file::ptr m_file;
	};

	void sync_one(metadb_handle_ptr track, const pfc::string8 & bpm_tag)
	{
		const char * path = track->get_path();
		if (std::strcmp(bpm_initial_key_field(path), "initialkey") != 0)
			return;   // not an MP4 container

		// The BPM as it now stands in the file, which is whatever the write
		// just put there - measured, tapped or scaled alike.
		double bpm = 0;
		try
		{
			metadb_info_container::ptr info = track->get_full_info_ref(fb2k::noAbort);
			const char * value = info.is_valid() ? info->info().meta_get(bpm_tag, 0) : nullptr;
			if (value != nullptr) bpm = std::atof(value);
		}
		catch (...) { return; }
		const long rounded = std::lround(bpm);
		if (rounded < 1 || rounded > 65535) return;

		mp4_tmpo_result result = mp4_tmpo_io_error;
		try
		{
			file::ptr f;
			filesystem::g_open(f, path, filesystem::open_mode_write_existing, fb2k::noAbort);
			fb2k_mp4_io io(f);
			result = mp4_set_tmpo(io, static_cast<std::uint16_t>(rounded));
		}
		catch (std::exception const & e)
		{
			FB2K_console_formatter() << "foo_rubato: tmpo not written, "
			                         << e.what() << ": " << path;
			return;
		}

		// Silence where it worked; a line where it did not, so a file whose
		// tempo players cannot see is not a mystery.
		if (result != mp4_tmpo_updated && result != mp4_tmpo_inserted && result != mp4_tmpo_unchanged)
			FB2K_console_formatter() << "foo_rubato: tmpo not written, "
			                         << mp4_tmpo_result_name(result) << ": " << path;
	}
}

completion_notify::ptr bpm_tmpo_sync_after(metadb_handle_list_cref p_tracks, const char * p_bpm_tag)
{
	metadb_handle_list tracks(p_tracks);
	pfc::string8 bpm_tag(p_bpm_tag);
	return fb2k::makeCompletionNotify([tracks, bpm_tag](unsigned code)
	{
		if (code != metadb_io::update_info_success) return;
		fb2k::inWorkerThread([tracks, bpm_tag]()
		{
			for (t_size i = 0; i < tracks.get_count(); i++) sync_one(tracks[i], bpm_tag);
		});
	});
}
