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

	//! For the tooltip, whose prose is not the thing under test - the figures
	//! in it are, and where they sit relative to one another.
	void check_has(const pfc::string8 & got, const char * want, const char * what)
	{
		g_checks++;
		if (std::strstr(got.get_ptr(), want) == nullptr)
		{
			std::fprintf(stderr, "FAIL: %s\n  wanted \"%s\" somewhere in:\n%s\n",
			             what, want, got.get_ptr());
			g_failures++;
		}
	}

	//! Where `want` starts, or -1. Two of these order a pair of lines.
	long at(const pfc::string8 & got, const char * want)
	{
		const char * found = std::strstr(got.get_ptr(), want);
		return found == nullptr ? -1 : (long) (found - got.get_ptr());
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

	// --- the tuning column's tooltip
	//
	// The prose is not under test and would be brittle if it were. What is
	// under test is that the figures reaching the reader are the same ones
	// that reach the RETUNE tags, that both reference pitches appear where the
	// era allows both, and that the order is the one suggest_retune ranked.
	{
		bpmcore::key_analysis t = k;   // -19.8 cents, which is A=435 exactly

		const pfc::string8 wartime = bpm_format_tuning_tooltip(t, 1941);
		check_has(wartime, "-19.8 cents from A=440", "the tooltip opens with the measurement");
		check_has(wartime, "+0.00%  to A=435", "and offers the pitch the side was cut at");
		check_has(wartime, "+1.15%  to A=440", "and the correction to concert pitch");
		check(at(wartime, "to A=435") < at(wartime, "to A=440"),
		      "1941 ranks A=435 first, which is what the era prior is for");
		check_has(wartime, "in range for 1941", "and says why there are two");
		check(std::strchr(wartime.get_ptr(), '\r') == nullptr,
		      "line ends are bare, so the Win32 side owns the CRLF");

		// Past the changeover only one pitch is in play, and a lone figure
		// needs no explaining.
		const pfc::string8 postwar = bpm_format_tuning_tooltip(t, 1952);
		check_has(postwar, "+1.15%  to A=440", "1952 corrects towards A=440");
		check(at(postwar, "to A=435") < 0, "and does not offer A=435 at all");
		check(at(postwar, "More than one") < 0, "and does not explain a list of one");

		// The same offset with no year is not a retune suggestion; it is a
		// measurement with nothing to measure it against.
		const pfc::string8 undated = bpm_format_tuning_tooltip(t, 0);
		check_has(undated, "no recording year", "an undated file says so");
		check(at(undated, "%  to A=") < 0, "and suggests nothing");
		check_has(undated, "ORIGINALDATE", "but says what would fix it");

		// Near the wrap there are three, and the reason is the measurement's
		// own ambiguity rather than the era's.
		t.tuning_cents = -47.3;
		t.near_wrap = true;
		const pfc::string8 near_wrap = bpm_format_tuning_tooltip(t, 1941);
		check_has(near_wrap, "semitone wrap", "a near-wrap tooltip warns about the key");
		check_has(near_wrap, "-3.00%  to A=440", "and carries the wrapped correction");
		check_has(near_wrap, "cannot tell a semitone apart", "and says where it came from");

		// Nothing measured, nothing to say about corrections.
		t = k;
		t.tuning_ok = false;
		const pfc::string8 unmeasured = bpm_format_tuning_tooltip(t, 1941);
		check_has(unmeasured, "No steady pitch", "an unmeasured track explains itself");
		check(at(unmeasured, "%  to A=") < 0, "and offers no correction");

		// A track the analysis declined has no tooltip at all, rather than an
		// empty box following the pointer around.
		t = k;
		t.ok = false;
		check(bpm_format_tuning_tooltip(t, 1941).is_empty(),
		      "a track with no analysis has no tooltip");
	}

	// --- the same thing as the whole row carries it
	//
	// Which is how it is actually shown: a report-mode list view offers its
	// tooltip by item, so the tuning cell alone was never asked for.
	{
		bpmcore::key_analysis tuned;
		tuned.ok = true;
		tuned.tuning_ok = true;
		tuned.tuning_cents = -19.8;
		const char * const title = "Corrientes y Esmeralda";

		const pfc::string8 row = bpm_format_row_tooltip(title, false, tuned, 1941);
		check_has(row, title, "the row tooltip names the row it belongs to");
		check_has(row, "+0.00%  to A=435", "and carries the tuning paragraph");
		check(at(row, title) < at(row, "-19.8 cents"),
		      "with the title first, where it introduces the rest");
		check_has(row, "Esmeralda\n\nTuning", "and a blank line between the two");

		// Nothing to explain and a title the column already shows in full: a
		// tooltip here would say back what is on the screen.
		bpmcore::key_analysis absent;
		absent.ok = false;
		check(bpm_format_row_tooltip(title, false, absent, 1941).is_empty(),
		      "an unanalysed row with a title that fits has no tooltip");

		// The one thing the control's own tooltip did, which this replaces.
		const pfc::string8 clipped = bpm_format_row_tooltip(title, true, absent, 1941);
		check_str(clipped, title, "a clipped title alone is the whole tooltip");
		check(at(clipped, "\n") < 0, "with no trailing blank line after it");

		// A row with no title still explains its tuning rather than going
		// silent, which is what an empty TITLE tag would otherwise do.
		const pfc::string8 untitled = bpm_format_row_tooltip("", false, tuned, 1941);
		check_has(untitled, "-19.8 cents", "a row with no title still explains itself");
		check(untitled.get_ptr()[0] != '\n', "and does not open with a blank line");
	}

	std::printf("foo_rubato_test: %d checks, %d failures\n", g_checks, g_failures);
	return g_failures == 0 ? 0 : 1;
}
