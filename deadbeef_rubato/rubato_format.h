#ifndef RUBATO_FORMAT_H
#define RUBATO_FORMAT_H

// Every string the DeaDBeeF plugin writes into a file, and the rules for which
// field it goes in.
//
// A port of foo_rubato/bpm_key_format.h, bpm_tag_fields.h and the year parsing
// in bpm_track_result.h, on std::string instead of pfc - the strings are the
// deliverable, so a file tagged by either host has to read the same, and a
// change to one of those headers wants making here too.
//
// Nothing here knows about DeaDBeeF either, so rubato_format_test checks it
// with no player running.
//
// Numbers are formatted and parsed by hand rather than through printf and
// strtod. Both follow LC_NUMERIC, and DeaDBeeF's GTK front end sets the locale
// from the environment - so on a German desktop "%.2f" writes "118,52" into
// the tag, and "118.52" read back parses as 118.

#include <cmath>
#include <cstring>
#include <string>

#include <bpmcore/bpmcore.h>

namespace rubato
{

// --- numbers ---------------------------------------------------------------

//! `value` with exactly `decimals` digits after a '.', whatever the locale.
//! A value that rounds to zero is written without a sign.
inline std::string format_fixed(double value, unsigned decimals)
{
	if (!std::isfinite(value)) return "0";
	unsigned long long scale = 1;
	for (unsigned i = 0; i < decimals; i++) scale *= 10;
	const unsigned long long n =
		static_cast<unsigned long long>(std::floor(std::fabs(value) * scale + 0.5));
	std::string out;
	if (value < 0 && n != 0) out += '-';
	out += std::to_string(n / scale);
	if (decimals > 0)
	{
		std::string frac = std::to_string(n % scale);
		out += '.';
		out.append(decimals - frac.size(), '0');
		out += frac;
	}
	return out;
}

//! The number at the start of `text`, accepting either '.' or ',' as the
//! decimal point. False where there is no number there at all.
inline bool parse_decimal(const char * text, double & out)
{
	if (text == nullptr) return false;
	const char * p = text;
	while (*p == ' ' || *p == '\t') p++;
	bool negative = false;
	if (*p == '+' || *p == '-') negative = (*p++ == '-');
	double value = 0;
	bool digits = false;
	for (; *p >= '0' && *p <= '9'; p++, digits = true) value = value * 10 + (*p - '0');
	if (*p == '.' || *p == ',')
	{
		p++;
		double place = 0.1;
		for (; *p >= '0' && *p <= '9'; p++, digits = true, place *= 0.1)
			value += (*p - '0') * place;
	}
	if (!digits) return false;
	out = negative ? -value : value;
	return true;
}

//! A signed number with a fixed number of decimals. Corrections are read by
//! their direction, so a plus is written; one that rounds to zero is "+0.00"
//! rather than "-0.00", which would be a direction there isn't.
inline std::string format_signed(double value, unsigned decimals)
{
	std::string out = format_fixed(value, decimals);
	if (out[0] != '-') out.insert(out.begin(), '+');
	return out;
}

enum bpm_precision
{
	bpm_precision_whole = 0,
	bpm_precision_1dp,
	bpm_precision_2dp
};

inline std::string format_bpm(double bpm, int precision)
{
	switch (precision)
	{
	case bpm_precision_1dp: return format_fixed(bpm, 1);
	case bpm_precision_2dp: return format_fixed(bpm, 2);
	default:                return format_fixed(bpm, 0);
	}
}

// --- tuning and key --------------------------------------------------------
//
// Each returns an empty string where there is nothing to say, and the caller
// treats an empty string as "do not write this field, and remove one already
// there". A field that survives a rescan it no longer describes is worse than
// a missing one.

inline std::string format_tuning(const bpmcore::key_analysis & key)
{
	if (key.ok && key.tuning_ok) return format_fixed(key.tuning_cents, 1);
	return std::string();
}

inline std::string format_key(const bpmcore::key_analysis & key)
{
	if (key.ok && key.best.root >= 0) return bpmcore::key_name(key.best.root, key.best.minor);
	return std::string();
}

//! "Dm:0.866 C:0.702 Gm:0.701"
inline std::string format_key_candidates(const bpmcore::key_analysis & key)
{
	std::string out;
	if (!key.ok) return out;
	for (int i = 0; i < key.candidate_count; i++)
	{
		if (key.candidates[i].root < 0) continue;
		if (!out.empty()) out += ' ';
		out += bpmcore::key_name(key.candidates[i].root, key.candidates[i].minor);
		out += ':';
		out += format_fixed(key.candidates[i].score, 3);
	}
	return out;
}

inline std::string format_key_confidence(const bpmcore::key_analysis & key)
{
	if (key.ok) return bpmcore::key_confidence_name(key.confidence);
	return std::string();
}

//! "40% major, 7 switches"
inline std::string format_mode_balance(const bpmcore::key_analysis & key)
{
	std::string out;
	if (!key.ok || key.major_fraction < 0) return out;
	out += format_fixed(100.0 * key.major_fraction, 0);
	out += "% major, ";
	out += std::to_string(key.mode_switches);
	out += key.mode_switches == 1 ? " switch" : " switches";
	return out;
}

//! "-1.72% to A=440", or blank for an unknown year or one from 1976 on.
inline std::string format_retune(const bpmcore::key_analysis & key, int year)
{
	std::string out;
	bpmcore::retune_option opt[bpmcore::key_candidate_count];
	if (!key.ok || !key.tuning_ok) return out;
	if (bpmcore::suggest_retune(key.tuning_cents, year, opt,
	                            bpmcore::key_candidate_count) < 1) return out;
	out += format_signed(opt[0].percent, 2);
	out += "% to ";
	out += bpmcore::retune_target_name(opt[0].target);
	return out;
}

//! "-1.72%@A=440 -2.83%@A=435 +2.94%@A=435", or blank when there is only the
//! one, which would repeat RETUNE.
inline std::string format_retune_candidates(const bpmcore::key_analysis & key, int year)
{
	std::string out;
	bpmcore::retune_option opt[bpmcore::key_candidate_count];
	if (!key.ok || !key.tuning_ok) return out;
	const int n = bpmcore::suggest_retune(key.tuning_cents, year, opt,
	                                      bpmcore::key_candidate_count);
	if (n < 2) return out;
	for (int i = 0; i < n; i++)
	{
		if (i != 0) out += ' ';
		out += format_signed(opt[i].percent, 2);
		out += "%@";
		out += bpmcore::retune_target_name(opt[i].target);
	}
	return out;
}

// --- the results window ----------------------------------------------------
//
// Ports of foo_rubato/bpm_result_format.h and the window half of
// bpm_key_format.h. Shared by every toolkit's results window, so a column
// cannot read differently between the GTK and the Cocoa one.

//! "±2.1", or blank where the track was too short to measure one.
inline std::string format_spread(double spread)
{
	if (spread > 0) return "\xc2\xb1" + format_fixed(spread, 1);
	return std::string();
}

//! "Dm (high)": the key never shown without how far to trust it.
inline std::string format_key_column(const bpmcore::key_analysis & key)
{
	std::string out = format_key(key);
	if (!out.empty()) out = out + " (" + bpmcore::key_confidence_name(key.confidence) + ")";
	return out;
}

//! "-19.8 c", "?" where there was no steady pitch, and "(!)" within 5 cents
//! of the semitone wrap, where the key beside it may be a semitone out.
inline std::string format_tuning_column(const bpmcore::key_analysis & key)
{
	if (!key.ok) return std::string();
	if (!key.tuning_ok) return "?";
	std::string out = format_signed(key.tuning_cents, 1) + " c";
	if (key.near_wrap) out += " (!)";
	return out;
}

//! What the tuning means and what to do about it, at the length it needs.
//! Lines are separated by bare newlines.
inline std::string format_tuning_tooltip(const bpmcore::key_analysis & key, int year)
{
	std::string out;
	if (!key.ok) return out;
	if (!key.tuning_ok)
	{
		return "No steady pitch was found in this track, so its tuning was not "
		       "measured. A spoken introduction, a run of applause, or surface "
		       "noise on its own all look like this from here.";
	}

	out += "Tuning is " + format_signed(key.tuning_cents, 1)
	     + " cents from A=440, and known only to within a semitone.";
	if (key.near_wrap)
		out += "\nThis one sits within 5 cents of the semitone wrap, so the key "
		       "beside it may be a semitone out.";

	enum { max_options = 4 };
	bpmcore::retune_option opt[max_options];
	const int n = bpmcore::suggest_retune(key.tuning_cents, year, opt, max_options);
	if (n < 1)
	{
		out += "\n\n";
		if (year <= 0)
			out += "This file carries no recording year, so there is no pitch to "
			       "correct towards - the offset is the same whether the side was "
			       "cut at A=435 or the transfer simply runs fast. Set ORIGINALDATE "
			       "or DATE and scan it again.";
		else
			out += "Recorded " + std::to_string(year) + ", by which time sides were cut "
			       "at A=440 alone, so an offset this size belongs to the transfer rather "
			       "than to a pitch standard.";
		return out;
	}

	out += "\n";
	for (int i = 0; i < n; i++)
		out += "\n    " + format_signed(opt[i].percent, 2) + "%  to "
		     + bpmcore::retune_target_name(opt[i].target);

	out += "\n\nA plus means play it faster.";
	if (n > 1)
	{
		bool wrapped = false, both_pitches = false;
		for (int i = 0; i < n; i++)
			for (int j = i + 1; j < n; j++)
			{
				if (opt[i].target == opt[j].target) wrapped = true;
				else both_pitches = true;
			}
		out += " More than one fits the same measurement";
		if (wrapped) out += ": the offset cannot tell a semitone apart";
		if (wrapped && both_pitches) out += ", and ";
		else if (both_pitches) out += ": ";
		if (both_pitches) out += "both reference pitches are in range for " + std::to_string(year);
		out += ". They are in the order that year makes likely.";
	}
	return out;
}

//! What the pointer resting on a row says: the title, where the column is
//! drawing it cut short, and the tuning explained. Blank for nothing to say.
inline std::string format_row_tooltip(const std::string & title, bool clipped,
                                      const bpmcore::key_analysis & key, int year)
{
	const std::string tuning = format_tuning_tooltip(key, year);
	if (tuning.empty() && !clipped) return std::string();
	std::string out;
	if (clipped || !tuning.empty()) out = title;
	if (!tuning.empty()) out += (out.empty() ? "" : "\n\n") + tuning;
	return out;
}

// --- which field -----------------------------------------------------------

inline bool equals_ascii_nocase(const char * a, const char * b)
{
	for (; *a != '\0' && *b != '\0'; a++, b++)
	{
		char x = *a, y = *b;
		if (x >= 'A' && x <= 'Z') x = static_cast<char>(x - 'A' + 'a');
		if (y >= 'A' && y <= 'Z') y = static_cast<char>(y - 'A' + 'a');
		if (x != y) return false;
	}
	return *a == *b;
}

enum container
{
	container_other,   //!< Vorbis comments, APE and the rest: the name is the field
	container_id3,     //!< mp3 and its relatives
	container_mp4
};

//! Which tagging scheme a file uses, from its extension - the same test
//! foo_rubato makes, and for the same reason: the field that reaches a
//! standard frame is named differently in each.
inline container container_of(const char * path)
{
	const char * ext = nullptr;
	for (const char * p = path; p != nullptr && *p != '\0'; p++)
	{
		if (*p == '.') ext = p + 1;
		else if (*p == '/' || *p == '\\' || *p == '|') ext = nullptr;
	}
	if (ext == nullptr) return container_other;
	static const char * const id3[] = { "mp3", "mp2", "mp1" };
	static const char * const mp4[] = { "m4a", "m4b", "m4r", "mp4", "m4v", "3gp", "3g2" };
	for (const char * e : id3) if (equals_ascii_nocase(ext, e)) return container_id3;
	for (const char * e : mp4) if (equals_ascii_nocase(ext, e)) return container_mp4;
	return container_other;
}

//! The field the BPM is written to, given the name on the preferences page.
//!
//! DeaDBeeF names fields after its own table, not after foobar2000's. The
//! ID3v2 TBPM frame is "BEATS_PER_MINUTE" there - "BPM" in an mp3 becomes a
//! TXXX frame that nothing reads - so the default name is moved onto the frame
//! players do read. In an MP4 "bpm" already maps onto the tmpo atom, and in a
//! Vorbis comment BPM is simply BPM. A name the user chose is left as it is.
inline const char * bpm_field(container c, const char * configured)
{
	if (c == container_id3 && equals_ascii_nocase(configured, "BPM")) return "BEATS_PER_MINUTE";
	return configured;
}

//! The tmpo atom holds a whole number, so an MP4 gets one whatever the
//! precision asked for.
inline int bpm_precision_for(container c, int precision)
{
	return c == container_mp4 ? bpm_precision_whole : precision;
}

//! Where players look for the key: TKEY in an mp3, which DeaDBeeF calls
//! INITIAL_KEY; the "initialkey" freeform atom in an MP4, which is the
//! beaTunes and Mixed In Key convention; INITIALKEY everywhere else.
inline const char * initial_key_field(container c)
{
	switch (c)
	{
	case container_id3: return "INITIAL_KEY";
	case container_mp4: return "initialkey";
	default:            return "INITIALKEY";
	}
}

//! How sure the rhythm classifier has to be before its answer becomes a
//! genre. See bpm_genre_min_confidence in foo_rubato/bpm_tag_fields.h for how
//! the figure was measured.
const double genre_min_confidence = 0.98;

//! The genre to write, or null to leave GENRE alone: only into an empty
//! GENRE, never "Other", and only at genre_min_confidence.
inline const char * genre_to_write(const char * existing_genre, const char * rhythm,
                                   double confidence)
{
	if (rhythm == nullptr || *rhythm == '\0') return nullptr;
	if (std::strcmp(rhythm, bpmcore::rhythm_name(bpmcore::rhythm_other)) == 0) return nullptr;
	if (!(confidence >= genre_min_confidence)) return nullptr;
	if (existing_genre != nullptr)
		for (const char * p = existing_genre; *p != '\0'; p++)
			if (*p != ' ' && *p != '\t') return nullptr;
	return rhythm;
}

enum attribution
{
	attribution_none,
	attribution_ours,
	attribution_foreign
};

//! Who an attribution field such as KeyAlgorithm says produced the value
//! beside it. `our_name` is "Rubato", without the version.
inline attribution attribution_of(const char * value, const char * our_name)
{
	if (value == nullptr || *value == '\0') return attribution_none;
	const std::size_t n = std::strlen(our_name);
	if (std::strncmp(value, our_name, n) == 0 && (value[n] == ';' || value[n] == '\0'))
		return attribution_ours;
	return attribution_foreign;
}

//! The year in a date tag, or 0. Accepts "1941", "1941-03-12", "12/03/1941".
inline int year_from_tag(const char * date)
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

//! Date fields in the order the recording year is looked for: the original
//! date first, which on a reissue is when the side was cut. The two
//! ORIGINAL_RELEASE names are what DeaDBeeF reads TDOR/TORY and APE's
//! ORIGINALYEAR into; "year" is its name for DATE and TDRC.
static const char * const year_fields[] = {
	"ORIGINALDATE", "ORIGINAL DATE", "ORIGINALYEAR",
	"ORIGINAL_RELEASE_TIME", "ORIGINAL_RELEASE_YEAR",
	"DATE", "YEAR"
};

}   // namespace rubato

#endif // RUBATO_FORMAT_H
