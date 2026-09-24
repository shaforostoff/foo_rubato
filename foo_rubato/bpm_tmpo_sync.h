#ifndef __BPM_TMPO_SYNC_H__
#define __BPM_TMPO_SYNC_H__

#include <SDK/foobar2000.h>

//! The completion notify to hand update_info_async wherever this component
//! writes a BPM.
//!
//! Once foobar2000 has finished writing, it reads each MP4 track's BPM back
//! out of the file and puts it in the `tmpo` atom as well, off the main
//! thread - see mp4_tmpo.h for why foobar2000's own write is not enough and
//! how little this one is allowed to change. Tracks in any other container
//! are skipped: TBPM and the Vorbis BPM field are already what players read.
//!
//! `p_bpm_tag` is the field the BPM was written to, captured now rather than
//! read from the preferences when the write completes.
completion_notify::ptr bpm_tmpo_sync_after(metadb_handle_list_cref p_tracks, const char * p_bpm_tag);

#endif // __BPM_TMPO_SYNC_H__
