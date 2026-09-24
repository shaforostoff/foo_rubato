#ifndef __BPM_TAG_FIELDS_H__
#define __BPM_TAG_FIELDS_H__

#include <pfc/pfc.h>

// Which field names reach the slots other players actually read, and whose
// attribution a field carries. pfc only, like bpm_key_format.h, so
// foo_rubato_test can check them without a host.

//! The field that puts the key where players look for it, for this file.
//!
//! There is no one name. foobar2000 writes a field under the name it is
//! given, except where it maps a name onto a native frame, and for the key it
//! maps exactly one: "INITIAL KEY", with the space, becomes the ID3v2 TKEY
//! frame. The same name in an MP4 file becomes a freeform atom literally
//! called "INITIAL KEY", which nothing reads; there the convention - beaTunes,
//! Mixed In Key - is a freeform atom named "initialkey", in lower case. In a
//! Vorbis comment it is INITIALKEY. All three were checked by writing each
//! name through foobar2000 and reading the raw frames and atoms back.
//!
//! AIFF and WAV are left on INITIALKEY because what foobar2000 does with the
//! spaced name in their ID3 chunks has not been checked.
inline const char * bpm_initial_key_field(const char * path)
{
	const char * ext = nullptr;
	for (const char * p = path; p != nullptr && *p != '\0'; p++)
	{
		if (*p == '.') ext = p + 1;
		else if (*p == '/' || *p == '\\' || *p == '|') ext = nullptr;
	}
	if (ext != nullptr)
	{
		static const char * const id3[] = { "mp3", "mp2", "mp1" };
		static const char * const mp4[] = { "m4a", "m4b", "m4r", "mp4", "m4v", "3gp", "3g2" };
		for (const char * e : id3)
			if (pfc::stricmp_ascii(ext, e) == 0) return "INITIAL KEY";
		for (const char * e : mp4)
			if (pfc::stricmp_ascii(ext, e) == 0) return "initialkey";
	}
	return "INITIALKEY";
}

//! How sure the rhythm classifier has to be before its answer becomes a genre.
//!
//! A dance left without a genre is no worse off than before the scan; a pop
//! song filed under Tango misleads every player and smart playlist that reads
//! the field, so this errs hard towards leaving it empty. Measured 2026-09-24
//! against 778 non-tango tracks in D:\m4a and 755 dances in TangoTunes whose
//! filenames give the genre:
//!
//!     probability >=   dances tagged   non-tango tagged as a dance
//!     any               99.9%           16 (Volare, La Mer, Mambo Craze...)
//!     0.95              98.8%            2
//!     0.98              95.9%            0
//!     0.99              91.0%            0
//!
//! Every vals and milonga clears 0.98; what is lost is 31 of 603 tangos. The
//! figure was read off the same 778 tracks it is judged on, so it is fitted -
//! but only towards leaving more genres empty, which is the safe direction.
const double bpm_genre_min_confidence = 0.98;

//! The genre to write, or null to leave GENRE as it is.
//!
//! Only into an empty GENRE, only a class that is a genre - "Other" is the
//! classifier saying it is none of the ones it knows, which is not something
//! to file a track under - and only at bpm_genre_min_confidence. A genre
//! already there is never replaced: it is somebody's decision, and nothing
//! records that an earlier scan was the one that wrote it.
inline const char * bpm_genre_to_write(const char * existing_genre, const char * rhythm,
                                       double confidence, const char * other_name)
{
	if (rhythm == nullptr || *rhythm == '\0' || strcmp(rhythm, other_name) == 0) return nullptr;
	if (!(confidence >= bpm_genre_min_confidence)) return nullptr;
	if (existing_genre != nullptr)
		for (const char * p = existing_genre; *p != '\0'; p++)
			if (*p != ' ' && *p != '\t') return nullptr;
	return rhythm;
}

//! Who an attribution field such as KeyAlgorithm says produced the value
//! beside it.
enum bpm_attribution
{
	bpm_attribution_none,      //!< no attribution at all
	bpm_attribution_ours,      //!< this component, any version
	bpm_attribution_foreign    //!< another tagger - beaTunes, most often
};

//! `value` is the attribution field's contents, or null; `our_name` is
//! FOO_RUBATO_ALGORITHM_NAME, passed in so this header needs no generated one.
inline bpm_attribution bpm_attribution_of(const char * value, const char * our_name)
{
	if (value == nullptr || *value == '\0') return bpm_attribution_none;
	const t_size n = strlen(our_name);
	if (pfc::strcmp_partial(value, our_name) == 0 && (value[n] == ';' || value[n] == '\0'))
		return bpm_attribution_ours;
	return bpm_attribution_foreign;
}

#endif // __BPM_TAG_FIELDS_H__
