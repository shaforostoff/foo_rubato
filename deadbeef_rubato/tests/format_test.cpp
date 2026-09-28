// The strings the DeaDBeeF plugin writes into files, checked without a player.

#include <clocale>
#include <cstdio>
#include <cstring>
#include <string>

#include "rubato_format.h"

namespace
{
	int failures = 0;

	void expect(const std::string & got, const char * want, const char * what)
	{
		if (got == want) return;
		std::printf("FAIL %s: got \"%s\", want \"%s\"\n", what, got.c_str(), want);
		failures++;
	}

	void expect(bool ok, const char * what)
	{
		if (ok) return;
		std::printf("FAIL %s\n", what);
		failures++;
	}

	std::string str(const char * s) { return s != nullptr ? s : "(null)"; }
}

int main()
{
	// A locale with a decimal comma, if the machine has one: none of these
	// may follow it. Where none is installed the checks still run in "C".
	const char * const commas[] = { "de_DE.UTF-8", "de_DE.utf8", "German_Germany.1252", "de_DE" };
	for (const char * l : commas)
		if (std::setlocale(LC_ALL, l) != nullptr) { std::printf("running under %s\n", l); break; }

	using namespace rubato;

	expect(format_fixed(118.52, 2), "118.52", "two decimals");
	expect(format_fixed(118.5, 0), "119", "rounds half up");
	expect(format_fixed(0.05, 1), "0.1", "rounds 0.05 up");
	expect(format_fixed(-19.79, 1), "-19.8", "negative");
	expect(format_fixed(-0.04, 1), "0.0", "no sign on a value that rounds to zero");
	expect(format_fixed(0.702, 3), "0.702", "leading zeros in the fraction");
	expect(format_fixed(3.004, 2), "3.00", "padded fraction");

	expect(format_signed(1.15, 2), "+1.15", "plus sign");
	expect(format_signed(-0.001, 2), "+0.00", "no minus zero");
	expect(format_signed(-2.83, 2), "-2.83", "minus sign");

	expect(format_bpm(118.456, bpm_precision_whole), "118", "whole BPM");
	expect(format_bpm(118.456, bpm_precision_1dp), "118.5", "one decimal BPM");
	expect(format_bpm(118.456, bpm_precision_2dp), "118.46", "two decimal BPM");

	double v = 0;
	expect(parse_decimal("118.52", v) && v > 118.519 && v < 118.521, "parse point");
	expect(parse_decimal("118,52", v) && v > 118.519 && v < 118.521, "parse comma");
	expect(parse_decimal(" 60", v) && v == 60, "parse whole");
	expect(!parse_decimal("", v) && !parse_decimal("abc", v) && !parse_decimal(nullptr, v),
	       "parse nothing");

	expect(container_of("/m/a.mp3") == container_id3, "mp3");
	expect(container_of("C:\\m\\a.M4A") == container_mp4, "m4a upper case");
	expect(container_of("/m/a.flac") == container_other, "flac");
	expect(container_of("/m.mp3/a") == container_other, "extension of a directory");
	expect(str(bpm_field(container_id3, "bpm")), "BEATS_PER_MINUTE", "mp3 BPM onto TBPM");
	expect(str(bpm_field(container_id3, "TEMPO")), "TEMPO", "a chosen name is kept");
	expect(str(bpm_field(container_mp4, "BPM")), "BPM", "m4a BPM is tmpo already");
	expect(bpm_precision_for(container_mp4, bpm_precision_2dp) == bpm_precision_whole,
	       "tmpo is whole");
	expect(str(initial_key_field(container_id3)), "INITIAL_KEY", "TKEY");
	expect(str(initial_key_field(container_mp4)), "initialkey", "mp4 key atom");
	expect(str(initial_key_field(container_other)), "INITIALKEY", "vorbis key");

	expect(str(genre_to_write("", "Tango", 0.99)), "Tango", "empty genre filled");
	expect(str(genre_to_write(nullptr, "Vals", 0.98)), "Vals", "missing genre filled");
	expect(genre_to_write(" ", "Tango", 0.97) == nullptr, "below the cutoff");
	expect(genre_to_write("Jazz", "Tango", 1.0) == nullptr, "existing genre kept");
	expect(genre_to_write("", "Other", 1.0) == nullptr, "Other is never a genre");

	expect(attribution_of(nullptr, "Rubato") == attribution_none, "no attribution");
	expect(attribution_of("Rubato;v=0.2.0", "Rubato") == attribution_ours, "ours");
	expect(attribution_of("Rubato", "Rubato") == attribution_ours, "ours, bare");
	expect(attribution_of("RubatoX", "Rubato") == attribution_foreign, "prefix only");
	expect(attribution_of("beaTunes;v=5", "Rubato") == attribution_foreign, "foreign");

	expect(year_from_tag("1941") == 1941, "year");
	expect(year_from_tag("1941-03-12") == 1941, "iso date");
	expect(year_from_tag("12/03/1941") == 1941, "european date");
	expect(year_from_tag("n/a") == 0 && year_from_tag(nullptr) == 0, "no year");

	bpmcore::key_analysis key;
	expect(format_key(key), "", "no key before analysis");
	expect(format_tuning(key), "", "no tuning before analysis");
	key.ok = true;
	key.tuning_ok = true;
	key.tuning_cents = -19.8;
	key.best.root = 2; key.best.minor = true;
	key.candidates[0].root = 2;  key.candidates[0].minor = true;  key.candidates[0].score = 0.866;
	key.candidates[1].root = 0;  key.candidates[1].minor = false; key.candidates[1].score = 0.702;
	key.candidates[2].root = 7;  key.candidates[2].minor = true;  key.candidates[2].score = 0.701;
	key.candidate_count = 3;
	key.confidence = bpmcore::key_confidence_high;
	key.major_fraction = 0.4;
	key.mode_switches = 7;
	expect(format_key(key), "Dm", "key");
	expect(format_key_candidates(key), "Dm:0.866 C:0.702 Gm:0.701", "key candidates");
	expect(format_key_confidence(key), "high", "key confidence");
	expect(format_mode_balance(key), "40% major, 7 switches", "mode balance");
	key.mode_switches = 1;
	expect(format_mode_balance(key), "40% major, 1 switch", "one switch");
	expect(format_tuning(key), "-19.8", "tuning");

	expect(format_retune(key, 0), "", "no retune without a year");
	expect(format_retune(key, 1990), "", "no retune after 1976");
	const std::string retune = format_retune(key, 1935);
	expect(!retune.empty() && (retune[0] == '+' || retune[0] == '-')
	       && retune.find("% to A=4") != std::string::npos, "retune for 1935");
	const std::string candidates = format_retune_candidates(key, 1935);
	expect(candidates.empty() || candidates.find("%@A=4") != std::string::npos,
	       "retune candidates for 1935");
	std::printf("1935, -19.8 cents: RETUNE \"%s\", RETUNECANDIDATES \"%s\"\n",
	            retune.c_str(), candidates.c_str());

	if (failures == 0) std::printf("all passed\n");
	return failures == 0 ? 0 : 1;
}
