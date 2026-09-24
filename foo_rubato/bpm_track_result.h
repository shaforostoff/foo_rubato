#ifndef __BPM_TRACK_RESULT_H__
#define __BPM_TRACK_RESULT_H__

#include <vector>

#include <SDK/foobar2000.h>

#include <bpmcore/bpmcore.h>

// What one scanned track came back with, as everything downstream sees it.
//
// One struct rather than a parallel array per field. There were six of those
// by the time the tempo alone was being reported - tempo, opening tempo,
// fluctuation, rhythm, adjusted, tag - and every one of them had to be sorted
// into the same order by hand in file_info_filter_bpm's constructor, with a
// dynamic_assert standing in for the compiler. Tuning and key would have made
// it a dozen.
struct bpm_track_result
{
	//! Tempo figures, at the metrical level the results window shows. Copies
	//! rather than readings from `analysis`, because the window's double and
	//! halve buttons scale them and the analysis should still say what it
	//! measured.
	double bpm = 0;
	double initial_bpm = 0;   //!< 0 where the opening had no beat to measure
	double spread = 0;        //!< 0 where the track was too short to measure one
	pfc::string8 rhythm;      //!< empty where no tempo was measured
	double rhythm_confidence = 0;   //!< the classifier's probability for `rhythm`, 0..1

	//! The user doubled or halved this row in the results window, so the
	//! analysis no longer stands behind the number and it loses its
	//! attribution.
	bool adjusted = false;

	//! Tuning and key, straight from the core.
	bpmcore::key_analysis key;

	//! The year the side was recorded, from the file's own date tag. No amount
	//! of signal processing supplies this, and without it there is no retune
	//! suggestion to make: the measured offset is the same either way, and
	//! only the year says whether it means A=435 or a slow transfer.
	int year = 0;
};

//! The year in a date tag, or 0. Accepts "1941", "1941-03-12", "12/03/1941".
//!
//! Four consecutive digits starting 18 or 19 or 20, anywhere in the string,
//! because the containers disagree about the format and some of them carry a
//! release year with a reissue date beside it.
inline int bpm_year_from_tag(const char * date)
{
	if (date == nullptr) return 0;
	for (const char * p = date; p[0] != '\0'; p++)
	{
		if (p[0] < '0' || p[0] > '9') continue;
		if (p[1] < '0' || p[1] > '9' || p[2] < '0' || p[2] > '9'
		    || p[3] < '0' || p[3] > '9') continue;
		const int year = (p[0] - '0') * 1000 + (p[1] - '0') * 100
		               + (p[2] - '0') * 10 + (p[3] - '0');
		if (year >= 1800 && year <= 2099) return year;
	}
	return 0;
}

//! The recording year for a track, from whichever date field the file carries.
//!
//! ORIGINALDATE first: on a reissue that is the year the side was cut, which
//! is the one the retune suggestion needs, while DATE is when the compilation
//! was put out.
inline int bpm_year_of(const file_info & info)
{
	static const char * const fields[] =
		{ "ORIGINALDATE", "ORIGINAL DATE", "ORIGINALYEAR", "DATE", "YEAR" };
	for (const char * field : fields)
	{
		const char * value = info.meta_get(field, 0);
		const int year = bpm_year_from_tag(value);
		if (year != 0) return year;
	}
	return 0;
}

#endif // __BPM_TRACK_RESULT_H__
