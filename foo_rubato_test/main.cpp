// Checks the strings the component writes into people's files.
//
// bpmcore_test covers the measurements; this covers what becomes of them. The
// two are separate programs because this one needs pfc for pfc::string8 and
// pfc::format_float, and bpmcore deliberately has no host in it.
//
// It links fb2k_pfc and bpmcore alone - not the foobar2000 SDK - so there is
// no host to initialise and nothing to install. That is only possible because
// bpm_key_format.h was kept clear of the preferences page; anything that
// reaches for a cfg_var belongs on the other side of that line.

#include <pfc/pfc.h>

#include <bpmcore/bpmcore.h>

#include "../foo_rubato/bpm_key_format.h"
#include "../foo_rubato/bpm_track_result.h"

#include <cstdio>
#include <cstring>

namespace
{
	int g_checks = 0, g_failures = 0;

	void check(bool ok, const char * what)
	{
		g_checks++;
		if (!ok) { std::fprintf(stderr, "FAIL: %s\n", what); g_failures++; }
	}

	void check_str(const pfc::string8 & got, const char * want, const char * what)
	{
		g_checks++;
		if (std::strcmp(got.get_ptr(), want) != 0)
		{
			std::fprintf(stderr, "FAIL: %s\n  got  \"%s\"\n  want \"%s\"\n",
			             what, got.get_ptr(), want);
			g_failures++;
		}
	}

	//! A result standing in for one measured track.
	bpmcore::key_analysis sample()
	{
		bpmcore::key_analysis k;
		k.ok = true;
		k.tuning_cents = -19.8;
		k.tuning_r = 0.61;
		k.tuning_ok = true;
		k.near_wrap = false;
		k.best.root = 2; k.best.minor = true; k.best.score = 0.866;   // Dm
		k.candidates[0].root = 2;  k.candidates[0].minor = true;  k.candidates[0].score = 0.866;
		k.candidates[1].root = 0;  k.candidates[1].minor = false; k.candidates[1].score = 0.702;
		k.candidates[2].root = 7;  k.candidates[2].minor = true;  k.candidates[2].score = 0.701;
		k.candidate_count = 3;
		k.margin = 0.164;
		k.confidence = bpmcore::key_confidence_high;
		k.major_fraction = 0.40;
		k.mode_switches = 7;
		k.mode_windows = 55;
		return k;
	}
}

int main()
{
	const bpmcore::key_analysis k = sample();

	// --- the tag schema, exactly as key-detection-feature-plan.md sets it out
	check_str(bpm_format_key(k), "Dm", "KEY");
	check_str(bpm_format_key_candidates(k), "Dm:0.866 C:0.702 Gm:0.701", "KEYCANDIDATES");
	check_str(bpm_format_key_confidence(k), "high", "KEYCONFIDENCE");
	check_str(bpm_format_mode_balance(k), "40% major, 7 switches", "MODEBALANCE");
	check_str(bpm_format_tuning(k), "-19.8", "TUNING");

	// A 1943 side reading 19.8 cents flat is simply at A=435 and wants nothing
	// done to it; the alternative, that the transfer runs slow, is offered
	// behind it along with the semitone wrap.
	check_str(bpm_format_retune(k, 1943), "+0.00% to A=435", "RETUNE at A=435");
	check_str(bpm_format_retune_candidates(k, 1943),
	          "+0.00%@A=435 +1.15%@A=440", "RETUNECANDIDATES at A=435");

	// The same measurement in 1950, when nobody was cutting at 435 any more,
	// is a transfer running slow and wants speeding up - and there is only the
	// one candidate, so the list is left off rather than repeating RETUNE.
	check_str(bpm_format_retune(k, 1950), "+1.15% to A=440", "RETUNE at A=440");
	check_str(bpm_format_retune_candidates(k, 1950), "", "one candidate writes no list");

	// Nothing to say, so nothing written - and the caller removes the field.
	check_str(bpm_format_retune(k, 0), "", "no year, no retune");
	check_str(bpm_format_retune(k, 1976), "", "1976 is past the window");

	// --- the results window
	check_str(bpm_format_key_column(k), "Dm (high)", "key column");
	check_str(bpm_format_tuning_column(k), "-19.8 c", "tuning column");

	// --- every field blank on a track nothing could be measured from
	bpmcore::key_analysis none;
	check(!none.ok, "a default key_analysis is not ok");
	check_str(bpm_format_key(none), "", "no key");
	check_str(bpm_format_key_candidates(none), "", "no candidates");
	check_str(bpm_format_key_confidence(none), "", "no confidence");
	check_str(bpm_format_mode_balance(none), "", "no mode balance");
	check_str(bpm_format_tuning(none), "", "no tuning");
	check_str(bpm_format_retune(none, 1943), "", "no retune");
	check_str(bpm_format_key_column(none), "", "no key column");
	check_str(bpm_format_tuning_column(none), "", "no tuning column");

	// --- a measurement too uncertain to report
	bpmcore::key_analysis vague = sample();
	vague.tuning_ok = false;
	check_str(bpm_format_tuning(vague), "", "an uncertain offset is not written");
	check_str(bpm_format_retune(vague, 1943), "",
	          "no retune is suggested from an offset that is not trusted");
	check_str(bpm_format_tuning_column(vague), "?", "but the window says so");

	// --- near the wrap, where the key can land a semitone out
	bpmcore::key_analysis wrapped = sample();
	wrapped.tuning_cents = 47.3;
	wrapped.near_wrap = true;
	check_str(bpm_format_tuning_column(wrapped), "+47.3 c (!)", "the wrap is marked");
	// This is where the semitone wraps earn their place: at 47.3 cents the
	// offset itself is 67 cents from A=435, too far to offer, while the same
	// grid read a semitone down is only 33 cents out and is the likeliest
	// answer of the three.
	check_str(bpm_format_retune(wrapped, 1943), "+1.92% to A=435",
	          "near the wrap the correction goes the other way");
	check_str(bpm_format_retune_candidates(wrapped, 1943),
	          "+1.92%@A=435 -2.70%@A=440 +3.09%@A=440", "all three wraps");

	// --- a track too short for the mode to be tracked
	bpmcore::key_analysis brief = sample();
	brief.major_fraction = -1;
	brief.mode_switches = -1;
	check_str(bpm_format_mode_balance(brief), "", "an untracked mode writes nothing");
	bpmcore::key_analysis one_switch = sample();
	one_switch.mode_switches = 1;
	check_str(bpm_format_mode_balance(one_switch), "40% major, 1 switch", "one switch");

	// --- keys are spelt by their signature, not by whatever is a semitone up
	bpmcore::key_analysis flat = sample();
	flat.best.root = 10; flat.best.minor = false;
	check_str(bpm_format_key(flat), "Bb", "Bb, not A#");
	flat.best.root = 3; flat.best.minor = false;
	check_str(bpm_format_key(flat), "Eb", "Eb, not D#");
	flat.best.root = 8; flat.best.minor = false;
	check_str(bpm_format_key(flat), "Ab", "Ab, not G#");

	// --- the sign is most of the meaning of a correction
	check_str(bpm_format_signed(1.7234, 2), "+1.72", "a positive correction is signed");
	check_str(bpm_format_signed(-1.7234, 2), "-1.72", "a negative one keeps its sign");
	check_str(bpm_format_signed(0.0, 2), "+0.00", "zero reads as no correction");
	check_str(bpm_format_signed(-0.001, 2), "+0.00",
	          "and so does a value that rounds to it from below");

	// --- the year, which is where the retune suggestion comes from
	check(bpm_year_from_tag("1941") == 1941, "a bare year");
	check(bpm_year_from_tag("1941-03-12") == 1941, "an ISO date");
	check(bpm_year_from_tag("12/03/1941") == 1941, "a day-first date");
	check(bpm_year_from_tag("1941-03-12 (reissued 1998)") == 1941,
	      "the first year wins, which on a reissue is the recording");
	check(bpm_year_from_tag("") == 0, "an empty date");
	check(bpm_year_from_tag(nullptr) == 0, "a missing date");
	check(bpm_year_from_tag("n/a") == 0, "a date with no digits in it");
	check(bpm_year_from_tag("12") == 0, "too few digits to be a year");
	check(bpm_year_from_tag("Track 03 of 1941 sessions") == 1941,
	      "a year embedded in prose, with a smaller number before it");
	check(bpm_year_from_tag("3012") == 0, "a four digit number that is not a year");

	std::printf("foo_rubato_test: %d checks, %d failures\n", g_checks, g_failures);
	return g_failures == 0 ? 0 : 1;
}
