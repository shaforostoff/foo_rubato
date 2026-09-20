# Key + tuning detection for foo_rubato

Plan of record, and now a record of what was built. Every number below was measured
against hand-made ground truth, not against another detector's output. Where something
was tried and rejected, it is recorded in
[What was tried and rejected](#5-what-was-tried-and-rejected) so it does not get
re-litigated.

**Status: implemented.** The analysis is `bpmcore/key.cpp`, behind `compute_key` and
`suggest_retune` in `bpmcore/bpmcore.h`; the tag text is `foo_rubato/bpm_key_format.h`;
the tags themselves are written by `foo_rubato/file_info_filter_bpm.cpp`. Two test cases
cover it: `key_synth` for the arithmetic and `tag_format` for the strings that reach
files. `rubato_key.py` (Python, numpy/scipy/ffmpeg) was the spec and stays as the
reference the port was checked against.

The numbers below are the C++ implementation's, re-measured after the port. Where they
differ from the Python's it is said so and why.

---

## 1. What ships

Three independent measurements, in increasing order of how much you can trust them.

| | output | measured accuracy |
|---|---|---|
| **Tuning** | cents from A=440, plus a confidence `R` | median error **2.2 c** at A=435 and **1.7 c** at A=440; recovers which reference the transfer was made at **98%** of the time |
| **Retune** | up to 3 ranked speed corrections | era priors calibrated from 281 hand-set transfer pitches; the first candidate names the right reference **98%** of the time |
| **Key** | best guess + 2 alternates + confidence | **60%** exact, **74%** tonic, **93%** within the three candidates |

The key detector is not accurate enough to present a single answer as fact. It is
accurate enough to be useful if the UI is honest about that, which is what the tag schema
below does.

---

## 2. Tag schema

```
KEY                Dm
KEYCANDIDATES      Dm:0.866 C:0.702 Gm:0.701
KEYCONFIDENCE      high
MODEBALANCE        40% major, 7 switches
TUNING             30.0
RETUNE             -1.72% to A=440
RETUNECANDIDATES   -1.72%@A=440 -2.83%@A=435 +2.94%@A=435
```

- `KEY` — single best guess, so players and other tools see something usable.
- `KEYCANDIDATES` — all three with their correlation scores. This is not a consolation
  prize: the top pick is right 60% of the time but the true key is in this list 93% of the
  time, and **100%** of the time in the high-confidence band.
- `KEYCONFIDENCE` — `high` / `medium` / `low` from the correlation margin. Calibrated, see
  §3. A player can trust `high` and treat the rest as a shortlist.
- `MODEBALANCE` — fraction of the track favouring the major of the relative pair, and how
  many times it switches. Describes pieces that genuinely move between relative keys.
- `TUNING` — cents from A=440, matching beaTunes' existing field so both can coexist.
- `RETUNE` / `RETUNECANDIDATES` — omitted entirely when the year tag is missing or ≥ 1976.

**One key tag, not two.** `KEY` and `INITIALKEY` are not worth splitting: the opening mode
differs from the predominant mode in **1 of 60** tracks. The interesting information is
the balance, and that is what `MODEBALANCE` carries.

---

## 3. Algorithm

### 3.1 Tuning offset

1. Decode mono at 22050 Hz.
2. STFT, 8192-point, hop 2048, Hann. Skip frames below 0.4 × median frame RMS.
3. Whiten each spectrum by dividing by a 101-bin running median. This flattens shellac
   rolloff and surface noise, and is what makes the peak picking work on 78s.
4. In 180–2200 Hz, keep local maxima more than 3× the local background; take the top 16
   by whitened magnitude; refine each with parabolic interpolation on the *raw* magnitude.
5. For each peak, `cents = 1200·log2(f/440)`, fold to `dev = ((cents+50) mod 100) − 50`.
6. Circular mean over the 100-cent circle, weighted by `log1p(magnitude)`. The angle is
   the offset; `R = |mean vector|` is the confidence.

Flag `R < 0.25` as low confidence, and flag `|offset| > 45` because near the wrap the
pitch-class binning below becomes unstable and the key can land a semitone out.

### 3.2 Chroma

`pc = round((1200·log2(f/440) − offset)/100) + 9 (mod 12)`, accumulated with
`log1p(magnitude)` weights. **Subtract the measured offset before binning** — this is the
step that makes key detection work on off-speed transfers.

### 3.3 Key

Correlate the whole-track chroma against the **Albrecht & Shanahan (2013)** profiles at
all 24 rotations. Report the top three.

```
major  .238 .006 .111 .006 .137 .094 .016 .214 .009 .080 .008 .081
minor  .220 .006 .104 .123 .019 .103 .012 .214 .062 .022 .061 .052
```

Confidence from the margin between first and second correlation:

| band | threshold | n | exact | true key in the 3 candidates |
|---|---|---|---|---|
| high | ≥ 0.104 | 21 | **86%** | **100%** |
| medium | 0.040–0.104 | 18 | 50% | 94% |
| low | < 0.040 | 19 | 42% | 84% |

**Name keys by their signature.** Flat keys get flat names — B♭, E♭, A♭, Gm, Cm, Fm. Tango
lives in those, and printing them as A#, D#, G# is wrong and will be the first thing
anyone notices.

### 3.4 Mode, from tracking rather than the profile

Take the key signature from §3.3, then decide major vs relative minor by tracking, not by
the profile's own mode. In 12 s windows at 3 s hop, score each window's chroma against the
major profile rooted on the signature's major tonic and the minor profile rooted on its
relative minor; take the majority.

Why: a tango that is minor in the A section and major in the B section **does not change
key signature** — it swaps which of the pair is in charge. Measured on the 60 labelled
tracks:

```
labelled MINOR (30)   mean 27% of windows favour major, median 10%
labelled MAJOR (28)   mean 74%,                         median 92%
78% correct  (majority-guess baseline 52%)
```

This lifts exact keys 57% → **60%** and tonic 71% → **74%**, and the result is flat across
thresholds 0.30–0.50, so use the natural 0.5 and keep it parameter-free.

**Reuse the frames, do not re-analyse the windows.** The Python slices the audio and runs
the whole spectral pass again per window, which at a 12 s window and a 3 s hop is four
times the work of the full pass. `key.cpp` keeps a 12-float chroma per frame - 93 KB for
a three-minute side - and sums the frames that fall in each window instead, so the mode
costs two correlations per window and nothing else. The only difference this makes is
that the silence gate is the track's rather than each window's, which is arguably the
better of the two. It was checked rather than assumed: over 25 tracks the two give
**identical keys**, and over all 133 the aggregate figures are the same.

### 3.5 Retune candidates

`A=435` is **−19.79 cents** from A=440. Speed change for a correction of `c` cents is
`2^(−c/1200) − 1`.

A measured offset is only known modulo 100 cents, so each era target yields three
candidates — the offset itself and its two semitone wraps. Rank by `|correction|/100 −
0.45·prior`, drop anything past 60 cents, keep the top three, de-duplicate targets within
2 cents.

Era priors, being the share of recordings still at A=435:

| year | P(435) | source |
|---|---|---|
| ≤ 1938 | 0.75 | data says 100%, shrunk — only two orquestas |
| 1939 | 0.70 | |
| 1940 | 0.65 | |
| 1941 | 0.50 | measured 43% |
| 1942 | 0.45 | measured 33% |
| 1943 | 0.42 | measured 29% |
| 1944 | 0.25 | measured 0%, shrunk |
| 1945–1975 | 0.00 | A=440 only |
| ≥ 1976, or no year | — | no suggestion at all |

These come from TangoTunes' own hand-set transfer pitches: Biagi 1927–1948 (145 dated)
and Troilo 1938–1945 (136). **The two orquestas switched three years apart** — Troilo was
on 440 from 1941, Biagi stayed on 435 through 1943 — which is why no single cutover year
works and why the window has to be generous. Pooled, by year:

| | 1938 | 1939 | 1940 | 1941 | 1942 | 1943 | 1944 |
|---|---|---|---|---|---|---|---|
| Troilo | 100% (2) | – | – | 0% (26) | 0% (28) | 0% (30) | 0% (28) |
| Biagi | 100% (8) | 100% (14) | 100% (22) | 100% (20) | 100% (14) | 87% (15) | 0% (11) |
| pooled | 100% | 100% | 100% | 43% | 33% | 29% | 0% |

**A shortcut worth taking.** Because the tuning measurement recovers the 435/440 reference
from audio alone 98% of the time, the era prior is really only needed to break the
modulo-100 ambiguity. If the measured offset is within a few cents of −19.8 or of 0,
say so directly rather than leaning on the year.

---

## 4. How it was built

- The FFT goes through `real_fft.h`, which already picks kiss or pffft and the width.
- **The geometry is in seconds, not samples**, as `odf.cpp`'s is. A 44.1 kHz file gets a
  16384-point window and the same 2.69 Hz per bin that 8192 points give at 22.05 kHz, so
  the whitening width, the search band and the peak threshold are all in Hz and
  rate-independent by construction. That is what lets the stage run on whatever the
  collector happens to hold rather than resampling the track a second time.
- **The 101-bin running median was the expensive part**, as predicted — 750 medians of
  101 values for every frame of every track, and 37% of the whole analysis when it first
  shipped. `running_median` in `key.cpp` keeps the window sorted and moves one element
  per step, which measured about ten times faster than an `nth_element` per bin. It then
  stopped searching for the two positions and counted them instead: over a sorted window
  the number of values below the one leaving is its index, and the number at or below the
  one arriving is one past where it lands, so two branchless passes do what two binary
  searches did, 1.8x faster, because the compiler vectorises a counting pass and cannot
  vectorise a search. A branchless binary search was tried first and was no faster, which
  is what showed the searching had never been the cost — the mispredicted branches were.
- Decode dominates a library scan, so tuning and key ride along on the decode the tempo
  analysis is already paying for. They are a separate spectral pass over the same buffer,
  not the same pass: a tempo wants a 46 ms window and a pitch wants 372 ms.
- **Measured cost**, one thread on a 196-second side: 0.082 s for the tempo alone,
  0.163 s with tuning and key. On all cores, 0.021 s and 0.040 s. So it roughly doubles
  the analysis and leaves it at 1200× realtime, against a decode of the same side that
  costs several times that. The conditional mode tracking floated above turned out not to
  be needed, because reusing the frames made it nearly free. (These are the figures after
  the median rewrite above; as first written the same side measured 0.097 s and 0.220 s,
  or 900×.)
- The key is computed **before** the tempo, so that a side whose tempo the grid never
  settles on still comes back with a key. The two answers stand or fall separately, and
  `progress_range` gives each stage half the progress bar.

**Checks that the port is faithful**, since none of the measured numbers mean anything if
it is not:

| | result |
|---|---|
| C++ against `rubato_key.py`, 25 tracks | **25/25 identical keys**; tuning agrees to two decimal places on every one |
| the 7 tracks where the tuning names the wrong reference | Python gives the same 7 offsets, to two decimals |
| pffft/float against kiss/double, 133 tracks | **133/133 identical keys**; tuning differs by at most 0.01 c, which is the print rounding |
| one thread against four | identical, as the rest of `bpmcore` already guarantees |

---

## 5. What was tried and rejected

Recorded so it is not attempted again. Three of these looked convincing on one or two
individual tracks and died against the labels — do not adopt anything here on the
strength of a single example. These were all measured with the Python reference on the
60-row basis, before the two reissues were noticed; the ranking is what matters and the
gaps are far wider than two tracks.

| approach | result |
|---|---|
| Krumhansl–Kessler profiles | 20% exact. Tuned for classical; unusable here. |
| Temperley profiles | 53%. Loses to Albrecht–Shanahan. |
| Two-stage: scale → signature, final cadence → tonic | 50%. Tracks transposition 12/12 and is more self-consistent than beaTunes (80% vs 62%), but self-consistency does not predict accuracy. |
| Blending the cadence into the 24-key score | Monotonically worse: 58 → 57 → 48 → 42 as the weight rises. |
| Harmonic-minor template, so minor keys are not matched through their relative major | 40%. Sound reasoning, worse result. |
| Reranking the top three by the cadence | 62%, but that is 2 tracks out of 60 after a 40-point grid search. Overfitting. |
| Chroma peakiness as a confidence measure | r = +0.076 with correctness — no signal. The margin gives +0.414. Combined is worse. |
| Key-signature change detection (Viterbi over 12 diatonic sets) | Failed its controls: unmodulated tracks reported **more** changes than manufactured modulations, across every threshold. Wrong question — see §3.4. |

---

## 6. Ground truth: what exists and what does not

This was the binding constraint all along, and it is worth writing down.

- **TangoTunes discography CSVs** — the only real key ground truth. They ship beside the
  FLACs in each release folder and carry hand-made `Pitch`, `Key Recording` and
  `Key Score`. Only the Troilo release had keys filled in: 60 rows, but two of those are
  reissues of a side already in the list, so it is **58 distinct recordings**. The
  Python's 62% counted both reissues twice and both are hits; on the 58 the same tracks
  give 60%. Nothing about the comparison between methods changes, but the headline figure
  does, and 58 is the honest number.
- **Joining the CSVs to the files is where the mistakes are.** Each release needs its own
  reader and each join needs checking against the filenames:
  - the Troilo CSV joins on its `N°` column, and has the two duplicate rows above;
  - the Biagi CSV has **two blank-headed columns before `Record`**, and the number that
    matches the filenames is the *second* of them. Joining on the first lines up only 69
    of 136 titles and silently mismatches the rest — it looked plausible and cost three
    points of accuracy before the titles were checked. Its numbering also restarts for
    the post-1948 section the release does not contain, so first occurrence wins.

  Both files are UTF-8 with BOM and CRLF. `evalcpp.py` and `biagi_eval.py` in the
  scratchpad do this; either way, **check the joined title against the filename** before
  believing a single figure that comes out.
- **beaTunes tags — not usable as truth.** Its key labels agree with *themselves* on only
  59–65% of recordings of the same composition; of 40 titles with ≥ 8 recordings, **zero**
  are unanimous. The failure pattern is detector noise, not transposition: the worst
  titles scatter across a tonic, its semitone neighbour and both modes. Disagreeing with
  beaTunes is not evidence of being wrong.
- **Partituras — a prior worth about four in five, not an adjudicator.** TangoTunes' own
  annotation shows `Key Recording` differs from `Key Score` **32%** of the time, every
  difference explicitly marked *(transposed)*. And the printed signature is a weak
  description of the sounding music: these scores carry **50–60 inline accidentals per
  page** — one chacarera is printed with an empty key signature and then uses 275 sharps.
  Two published arrangements of *Nada* are a fifth apart.
- **Scraped or LLM-sourced key lists — worthless.** A 50-entry list of orquesta keys
  agreed with beaTunes **3%** of the time, below the 15% chance rate for its own
  distribution, and drew 82% of its entries from six "expected" minor keys.
- **Engraved PDFs are exact and cheap.** Finale/Maestro scores position the key signature
  as text glyphs (`&` clef, `b` flat, `#` sharp; subset fonts shift these to U+F000).
  `pdfkey.py` reads them with no OMR — 7/7 verified. Apply `page.rotation_matrix` to the
  char boxes and skip small cue-staff clefs.

---

## 7. Open questions

1. **More labels are the only thing that moves the number.** 58 distinct recordings is
   enough to rank methods coarsely but not to separate Albrecht–Shanahan from Temperley
   (58 vs 53 is 3 tracks), and not to test the reranking discarded as overfitting.
   200–300 would settle both. Sources, cheapest first: other TangoTunes releases whose
   CSVs have `Key Recording` filled in, then hand labels.

   The harness for this is in place. `bpmcore_test key_batch <list>` takes a file of
   `path<tab>year` lines and writes one TSV row per track — offset, confidence, all
   three candidates with their scores, the margin, the mode balance and the retune
   suggestion — so scoring a new release is building the list and joining it back.
   `evalcpp.py` in the scratchpad does both ends for a Troilo-shaped CSV.
2. **The dominant bias is the biggest single error class** — 11 of 23 misses read a
   fifth above the true tonic. On the Caló recordings of *Nada* it ranks C above F
   consistently. No principled fix has survived testing yet.
3. **Relative confusion can come with high confidence.** *Maldito corazón* reads Bm at
   margin 0.229 where it should be D major — right signature, wrong relative. So the
   margin does not protect against this specific error, and the candidate list is the
   only mitigation. Mode tracking helps on average and does not help here.
4. **Cross-collection tuning.** Where the same recording exists in TangoTunes and
   elsewhere, the TangoTunes offset is the reference and the difference is the exact
   retune amount — better than any era prior. Needs a recording-identity match
   (artist + title + year is not reliable enough on its own).
5. **The era priors are two orquestas.** 281 sides is a lot of sides and not many
   orchestras, and the shrinkage in the 1939–1944 window is a guess at how much the two
   generalise. Any further release with a hand-set `Pitch` column would sharpen it, and
   the by-year table in §3.5 is the thing to re-measure.
