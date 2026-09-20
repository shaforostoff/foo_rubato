Rubato BPM Analyzer for foobar2000
==================================

Change Log
----------

### Unreleased

* **Tuning and key detection**, off the decode the tempo analysis already
  pays for. Three measurements, in increasing order of how far they can be
  trusted: where the recording sits against A=440 in cents; what key it is in,
  with two alternates and a confidence band; and - given the recording year -
  what speed correction would put the transfer back on pitch.

  The key detector is not accurate enough to present a single answer as fact,
  and the tag schema says so rather than hiding it. `KEY` is right about 60%
  of the time; the true key is among the three in `KEYCANDIDATES` 93% of the
  time, and in the top confidence band every time. `KEYCONFIDENCE` says which
  of those numbers applies to the track in hand. The three are written
  together or not at all.

  Everything was measured against TangoTunes' hand-made discography data -
  58 Troilo sides with a hand-written recording key, 130 Biagi transfers with
  a hand-set pitch - and not against another detector's output. The tuning is
  the stronger of the two: a median error around 2 cents, and 98% correct at
  recovering whether a transfer was made at A=435 or A=440 from the audio
  alone. `key-detection-feature-plan.md` records how, and the eight
  approaches that were tried and rejected along the way; three of those
  looked convincing on one track and died against the labels.

* The mode is decided by tracking rather than by the key profile's own answer.
  A tango that is minor in the A section and major in the B section does not
  change key signature - it swaps which of the signature's two tonics is in
  charge - so the signature comes from the whole-track chroma and the mode
  from a majority over 12-second windows. That lifts exact keys from 57% to
  60% on the labelled sides, and the tonic alone from 71% to 74%;
  `MODEBALANCE` reports what it found.

* The retune suggestion needs the recording year, which no amount of signal
  processing supplies; the component reads `ORIGINALDATE` first and falls back
  to `DATE`. Because a measured offset is only known modulo a semitone, and
  because through the 1939-1944 transition both reference pitches were in use,
  there is more than one answer and `RETUNECANDIDATES` carries the rest. The
  per-year priors are measured from 281 dated sides. Nothing is suggested for
  a track with no year or one recorded from 1976 on.

* The key gets its own attribution, `KeyAlgorithm`, carrying the same
  `Rubato;v=<version>` the BPM's does and named the way other taggers already
  name it. Two fields rather than one because the two measurements are
  overruled separately: doubling a BPM in the results window says nothing
  about the key beside it, and a track with no steady pitch in it still
  produced a BPM worth attributing. It stands behind a `KEY` this scan wrote
  and nothing else, so a rescan that measures none removes it along with the
  key fields rather than leaving credit behind for a field that is no longer
  there. The preferences checkbox now reads *Write BpmAlgorithm and
  KeyAlgorithm* and governs both.

* Detection costs more than the tempo analysis itself - a 196-second side goes
  from 3200x realtime to 1300x on one thread - which is still a small fraction
  of what decoding it costs. It can be switched off on the
  preferences page, along with each of the three groups of fields separately.

* `bpm_track_result` replaces the parallel arrays the results window and the
  tag writer were passed. There were six of them for the tempo alone, each
  having to be sorted into the same order by hand with a `dynamic_assert`
  standing in for the compiler; tuning and key would have made it a dozen.

* Two new test cases. `key_synth` checks the tuning offset, the key and the
  retune arithmetic against synthesised chords whose answers are known - that
  the offset comes back, that taking it out leaves the key where it was, and
  that transposing the audio transposes the answer at all twelve semitones.
  `tag_format` checks the exact strings that reach people's files. Neither
  needs any audio on disk.

* **The analysis is about 1.4x faster on one thread, and 2x in the 32-bit
  build**, without a single figure moving: over the 135 Troilo sides both the
  key output and the tempo output come back byte for byte identical to what
  they were. Three loops were doing more work than they had to, and profiling
  the stage timings said which.

  The spectral whitening's sliding median - which on its own was 37% of the
  whole analysis - stops searching for where values belong. Over a sorted
  window the count of values below the one leaving *is* its index, and the
  count at or below the one arriving is one past where it lands, so two
  branchless passes replace two binary searches. The compiler vectorises the
  passes and cannot vectorise a search. A branchless binary search was tried
  first and was no faster, which is what showed the searching was never the
  cost: the mispredicted branches were.

  The onset envelope's half-wave rectifier is written as a select rather than
  a branch. The sign of a flux difference is not predictable and adding a zero
  changes no sum, so this is the same arithmetic in the same order - six times
  faster over that loop, for one line.

  The key stage's frame RMS reads each sample once instead of four times. Its
  windows overlap four to one, so one sum per hop added up in fours is the
  same figure; the association changes, so the last bits of the sum can, and
  over 135 sides not one frame changed sides of the silence gate.

* `autocorrelate` hands its per-window results to the caller rather than
  copying them and then reading its own copy. On a long side that is a
  megabyte that was live twice for no reason.

* **The autocorrelation carries four running sums instead of one**, which is
  4.7% off the whole analysis at 22.05kHz and 2.8% at 44.1kHz. A single total
  is a chain of additions each waiting on the one before it, and no compiler may
  reorder a floating-point sum to break that chain, so the vector unit idles
  through the densest loop in the tempo stage. Four independent sums fill it,
  and are as deterministic as one - which is what an answer that becomes a tag
  depends on.

  Reassociating a sum can change its last bits, so this was not argued from the
  source but run: over all 12,163 tracks the output is byte for byte what it
  was.

* **The onset envelope forms its magnitudes at single precision**, a further
  8.6% at 22.05kHz and 7.1% at 44.1kHz - and with the sums above, 13% and 10%.
  `logf` and `sqrtf` are about a third cheaper than the double versions, and
  this is the largest single loop in the analysis. What comes out is kept as
  double, so the difference between frames that the detector actually runs on
  is unchanged.

  This one does move the last digits, so it was measured rather than counted
  exact: over the same 12,163 tracks not one changed rhythm, meter or metrical
  level, the largest tempo difference anywhere was 0.0005 BPM, and the 3,692
  hand taps score the same to two decimals. The model was fitted on float32
  magnitudes to begin with - `stft_mag` in `scripts/analysis/odf.py` - so this
  narrows towards the arithmetic the trees were trained on rather than away
  from it. `docs/tango-analysis.md` carries the run.

* **A row of results explains itself on hover.** The Tuning column has room
  for a number and the window has room for the column, so what makes the number
  actionable had nowhere to go: which reference pitch, in which direction, by
  how much, and why there is more than one answer. Resting the pointer anywhere
  on the row now gives all four - the same corrections that reach the RETUNE
  tags, ranked the same way, so a side from the 1939-1944 transition shows both
  A=435 and A=440 with the era's own order. A title the column cut short is
  shown in full above it.

  It also says what it cannot do. A file with no recording year gets the
  measurement and an explanation of why no correction follows from it; a track
  whose pitch never settled says so rather than showing a number nobody should
  act on; and an offset near the semitone wrap carries the warning that the key
  beside it may be a semitone out.

* **The component ships on PFFFT at single precision** rather than KISS FFT at
  double. The transform is about six times faster at the sizes used here, and
  on a 196-second side that is 0.163s against 0.261s for a whole analysis on
  one thread. The 64-bit DLL is 9KB smaller for it.

  This was measured before and not taken: over the 12,157 tracks two builds
  both analysed, not one changed its rhythm, its meter or its metrical level,
  and tap accuracy came out identical to two decimals against 3,692 hand taps -
  but a measurement showing nothing moved is not by itself a reason to move.
  What changed is what the transform is a share of. With the three loops above
  out of the way it is a larger fraction of a shorter run, so the same swap is
  worth 1.60x where it used to be worth 1.42x.

  KISS FFT is still vendored, still built, and still the oracle
  `fft_backend_test` checks the shipping transform against - not a fallback.
  `-Kiss` on Windows and `--kiss` on macOS build it, each into its own build
  directory and its own archive name, as `-Pffft` and `--pffft` used to do for
  the other one. `docs/tango-analysis.md` carries the evidence and the two
  things the switch means.

### Version 0.1.0

* **It runs on macOS.** `scripts/build_release_macos.sh` builds one universal
  bundle - Apple Silicon and Intel - and packages it as
  `mac/foo_rubato.component` inside a `.fb2k-component`, which is where
  foobar2000 for Mac 2.6 and newer looks. Given the archive the Windows script
  produced, `--merge` folds the two together into a single download that
  installs on either platform; each host takes the folder it knows and ignores
  the rest.
* The analysis did not move to get there. `bpmcore` was already free of
  foobar2000, pfc, ATL and `windows.h`, and it compiled for macOS unchanged -
  the port is the shell around it. What was Win32 in that shell was the window
  system and three small things beside it: `uMessageBox` became the SDK's own
  cross-platform `fb2k::messageBox`, `SetThreadPriority` became
  `QOS_CLASS_UTILITY`, which is macOS's name for the same intent, and
  `sscanf_s` became `sscanf`, which for `%f` differs in nothing but which
  compilers have it.
* The windows are Cocoa, written in code rather than drawn in a nib, under
  `foo_rubato/mac/`. `foo_rubato/bpm_ui.h` is the line between the two
  platforms: two functions, one to put up the results window and one the tap
  window, which is all the rest of the component knows about either. The
  preferences page has no Apply button there and cannot have one - the macOS
  preferences API hands back an `NSViewController` and that is the whole
  contract - so each setting is written as it is changed, which is how
  foobar2000 for Mac's own pages behave.
* The BPM tag name is now read through `bpm_tag_name()` rather than off the
  `cfg_var` directly. `cfg_string` is not one class: the SDK targets API 80 on
  Windows and API 81 on macOS, and those select two implementations that share
  no interface - the legacy one *is* a `pfc::string8`, the modern one keeps its
  value behind `get()`. A `pfc::string8` is what both will give.
* The tap window registers a tap on the way down rather than on the click
  completing, which is the TODO the Windows dialog carries: a click held for
  80ms is 80ms of error in a measurement whose entire content is when the
  button went down.
* The component is ad-hoc signed, and that is all foobar2000 for Mac asks for -
  it runs under the hardened runtime but ships
  `com.apple.security.cs.disable-library-validation`, so it will load code
  signed by somebody else or by nobody. Signing at all is not optional: Apple
  Silicon will not map unsigned code, and cross-building a universal binary
  does not sign it for you.
* `scripts/get_sdk.sh` fetches the SDK for a macOS build, and does not repeat
  the pin: it reads the release, the URL and the checksum out of
  `scripts\get_sdk.ps1`, which stays their one home. The about box states the
  SDK release it was built against from that same file, so a second pin would
  make it wrong on one platform and right on the other with nothing to say
  which.
* `scripts/build_release_macos.sh` builds with one fewer job than the machine
  has cores rather than with all of them, and takes `-j` to say otherwise. A
  job is a clang holding a translation unit with the SDK precompiled into it,
  and a machine with other work on it can run out of memory and have the build
  killed with an exit code and nothing else.
* `dialog_test` is Windows only now. It draws dialog templates out of the built
  DLL, and there are none in the macOS component - its windows are built in
  code, where a label that does not fit is a layout constraint rather than a
  resource. The other five harnesses run on both.
* Every binary is 87,000 bytes smaller, and the rhythm model answers the same.
  Its thresholds were stored as `double` but chosen as `float` - scikit-learn
  bins `float32` features, so all 10,500 of them were float values widened -
  and `classify` compares a `double` feature against one, which widens it
  straight back to the number the fit produced. Not a split moves. The
  generator refuses to narrow a threshold that a later refit has moved off that
  grid rather than shipping a model that is quietly half an ulp different.
* The 11,250 leaf values are `float` too. Those do round, by up to half an ulp
  each, but a class score is one leaf per tree accumulated in `double`, so the
  error is bounded rather than compounding: 5.0e-06 worst case against scores
  of order one, worked out from the model itself so that a refit widens the
  bound instead of overrunning it. On the 61 reference cases the scores move by
  7.6e-08, no track changes class, and the gap between the component's
  confidence and scikit-learn's own is unchanged at 5.0e-07. This is the half
  of the model that does not compress, so it is also the only part where the
  download shrinks with the binary - about 44,500 bytes per copy.
* A scan of a long track holds much less memory. The audio buffer is sized
  from the length the metadb already knows rather than from a nominal four
  minutes, so a track past that no longer doubles its buffer and copy itself
  into the larger one - which had both resident at once. A fifteen minute side
  at 44.1kHz peaks at 152MB where it used to reach 273MB, and a six minute one
  at 61MB where it reached 101MB; a track under four minutes is unchanged,
  because the reserve was never touched pages. That is per scanning thread, and
  there are cores-2 of those.
* **Reggae is a fifth rhythm**, beside Tango, Vals and Milonga. It is there for
  the tempo rather than for the label: what a dancer taps in a reggae is the
  quarter note, and what the grid returns is usually the skank an octave above
  it, so the two candidate readings differ by a factor of two. The old
  four-class model had no prior that could choose between them - "other" spans
  bossa to disco and its prior is deliberately near-flat - and across 67 reggae
  sides the autocorrelation supported the two levels within a median of 0.017 of
  each other. The answer was a coin toss, and it landed on double the tapped
  tempo on 10 of the 14 sides that carry a hand tap. With a prior of its own it
  is 9 of those 14 cross-validated, up from 4, and all seven of the sides the
  classifier actually calls reggae are right; on the built component, over all
  16 sides there is now a tap for, 15.
* The reggae prior is centred at 88 BPM rather than at the 81 its own taps
  average, because a prior's centre is a boundary and not an average: a
  log-normal separates two metrical levels at `mu * sqrt(2)`, and 88 puts that
  at 124.5 - between the fastest quarter note tapped, 120, and the slowest
  skank the grid returns, 128. Centred on the average it reads *Kingston Town*,
  *Mark Of Slavery* and *Is This Love* an octave down.
* Reggae recall is 43% cross-validated, from 68 labelled examples against
  tango's 8,523. A side it misses behaves exactly as it did before, so the
  failure is quiet; precision, which is the figure that can do harm, is 80.6%.
  Classification overall went from 94.2% to 93.6% and BPM within 2 BPM of the
  tap is unchanged at 88.7%.
* Tango ends 27 sides worse and milonga 7, vals 10 better, but reggae is not
  what moved them: it takes seven tracks in the whole collection, one of them a
  tango. Those net figures sit on top of a churn of about 130 sides between the
  four classes that already existed, which is what refitting does: refitting the
  four-class model under a different fold split, which cannot mean anything,
  moves 287 of the 12,160 tracks where adding the fifth moves 301. Reggae takes
  no milongas at all - over all 753 of them the model gives it a mean
  probability of 0.0001 and never ranks it second. Against the hand taps 18 tracks land closer and 15
  further, five of the 18 being reggae. `docs/tango-analysis.md` has both
  confusion matrices and the tracks that moved furthest.
* Disco, funk, cumbia and salsa were measured at the same time and left alone.
  They are already reported at the tempo they are tapped at, so a class would
  buy a name and not a number, and the metrical features cannot separate them
  anyway - 3 to 18% recall against reggae's 43%. `docs/cortina-genres.md`
  records what was measured and what it would take.
* Renamed. BPM Analyser is now Rubato BPM Analyzer, the component is
  `foo_rubato.dll` rather than `foo_bpm.dll`, and the version starts again at
  0.1.0. Nothing about the analysis changed with the name; 0.4.2 below and
  0.1.0 here are the same code.
* It installs alongside BPM Analyser. foobar2000 tells components apart by
  filename, and every GUID is new, so the two keep separate preferences pages,
  separate context menus and separate settings, and can be run side by side to
  compare them. Point them at different BPM tag names before doing that, or
  they will overwrite each other's answers.
* **Tagging** on the preferences page gained two checkboxes, both on by
  default: whether to write `INITIALBPM` and whether to write `BpmAlgorithm`
  beside the BPM. Someone who wants nothing in their files but the BPM itself
  can now say so, without giving up the analysis that produces the rest.
* Unchecking one stops the field being *added*; it does not license leaving
  behind a claim known to be false. Removal stays unconditional, so tapping a
  BPM by hand over a measured one still clears the attribution, and a BPM with
  no measurable opening still clears `INITIALBPM`, whatever the checkboxes say
  - the alternative is a file describing a measurement that is no longer
  there. The switches do cost the reverse inference: with the attribution
  turned off, a missing `BpmAlgorithm` no longer means the BPM was tapped.
* The preferences page is 152 dialog units tall, the *Tagging* group having
  grown by 30 to hold them.
* The results window shows a **BPM from tag** column when at least one of the
  scanned tracks already carried a BPM tag, so a fresh measurement can be read
  against the value that was there - a hand tap, on this collection. The column
  is absent when no track had one. It shows the string the file carried rather
  than a reformatted number, because a whole number and a decimal mean
  different things here.
* Every track in the selection is analysed. It used to be that one selected
  track without a BPM tag caused every track that had one to be dropped,
  silently, so asking for twenty tracks could return a single row - and the
  **BPM from tag** column above could never be filled in exactly the case it
  was added for. Analysing writes nothing to the files on its own; the results
  window is where that is decided. The one exception is *Write tags
  automatically*, which skips the window, so when that is on and some of the
  selection already carry a BPM tag the component asks first, offering to
  analyse all of them, only the untagged ones, or none. A track whose info
  foobar2000 has not read yet is still skipped, but now says so in the console
  rather than vanishing.
* The results window's commit button says how many files it is about to write
  - *Update 137 files*, or *Update file* for one. It writes every track in the
  list, which the old fixed *Update files* left open to being read as the
  selection; the selection drives the double and halve buttons only. The button
  went from 50 dialog units to 74 to hold the count - *Update 99 files* already
  needed more than the fixed label did - which is five digits with room to
  spare. It measures its own label at run time too, and widens if it has to,
  which only a host drawing the page in a wider face than the template's can
  bring about.
* Every checkbox on the preferences page carried `BS_CENTER`, which centres a
  label in its control rather than setting it against the box. *Write tags
  automatically* nearly fills its 93 units so it looked flush, but the two new
  boxes are much shorter than theirs and appeared indented from the two above
  them. Checkbox text belongs hard against the box, which is the default, so
  the flag is gone. The widths went up with it - 130 in *Tagging*, 300 for the
  console switch - as margin rather than as a fix: the labels all fitted, and
  with the text left aligned the extra width only makes the click target reach
  the end of the label instead of stopping short of it.
* `dialog_test` draws the dialog templates out of the built DLL and checks that
  every label fits the control around it, so a layout fault no longer needs
  foobar2000 - or an eye - to find. `scripts\render_dialogs.ps1` writes one PNG
  per dialog; the check alone runs as the `dialog_labels` CTest case and so is
  part of every release build. It was written after the second of two faults
  reached a release unseen, and the first thing it did was reproduce the one
  above and then confirm its removal.
* Tracks are scanned several at a time rather than one after another - two
  short of what the machine reports, and never fewer than one. Decoding is the
  whole cost of a scan: a 138-second side is a tenth of a second of analysis
  against seconds of decoding, so reading one track at a time left most of the
  machine idle for the entire wait. Two cores are held back because a scan is
  something a DJ starts in the middle of a set, and the scanning threads run
  at below-normal priority as well, which is the part that actually keeps
  playback smooth - on a machine with hyperthreading, subtracting two from the
  logical processor count leaves two hyperthreads rather than two cores.
* Each scan keeps its analysis on its own thread instead of spreading the
  spectral stage across the machine, which with whole tracks already running
  side by side would be the same cores counted twice; a lone track still gets
  the machine. No tag can change as a result: `bpmcore_test tempo_spread`
  checks the analysis is bit-identical for one thread, two and all of them,
  which is what makes the thread count safe to vary.
* The progress dialog now reports the tracks in flight - "a.flac, b.flac and 4
  more" - with the average of their progress on the second bar. All of it is
  written by the one thread `threaded_process` started, which no longer scans
  anything itself: `threaded_process_status` is handed to that thread and
  nothing in the SDK promises it is safe from several at once.
* Results are recorded at each track's own index rather than appended, so the
  results window stays in the order the tracks were selected however the scans
  interleave.
* The legacy 2009 engine is deleted, and the preferences page with it. It was
  off by default and unreachable without the advanced switch, had no test
  coverage - both harnesses link bpmcore alone and it lived in the component -
  and had not been touched substantively since the 2025 SDK port. It also
  produced two wrong outputs whenever it was switched on: it filled in only a
  BPM, so the rhythm column read "Other" for every track where no classifier
  had run, and its results were stamped `BpmAlgorithm=Rubato` for a number the
  2009 algorithm produced, which defeats the point of an attribution.
* That removes 873 lines - the engine, its FFT wrapper and its maths header -
  and nine of the preferences page's controls with them: seconds per sample,
  samples per song, sample offset range, calculated BPM range, BPM candidate
  result, interpolate flux, and the three FFT settings. Every one of them was
  read only by the deleted engine; the tango engine's geometry is fixed by the
  model it was fitted to. *Calculated BPM range* was the one worth being rid
  of: it defaulted to 75-195, and a milonga at 54 or a vals at 61 sits outside
  that, so had it ever applied to the new engine it would have broken it.
* What is left on the page is what a user would actually choose: **Tagging**
  (BPM precision, tag name, write without showing the results), **Manual
  Analysis** (taps to average, reset pause), and **Diagnostics** (console
  output). The console switch was the one live control stranded in the dead
  group, and its label no longer promises a BPM candidate list. That took the
  page from 296 dialog units to 122.
* Nothing is registered under Preferences > Advanced > Tools any more, so the
  branch is not registered either - an empty node would be worse than none.
* The about box says what the component now does rather than "automatically
  analysing the BPM of audio files", and carries a 2026 copyright for Nick
  Shaforostov alongside the original authors'.
* The foobar2000 SDK release the about box quotes is read out of
  scripts\get_sdk.ps1, where the download and its checksum are already pinned,
  instead of being a second copy of the date. CMake reconfigures when that
  script changes, so the two cannot drift the way the version once did.
* The rhythm is no longer written to a tag, and the two advanced-config entries
  that controlled it - *Write the detected rhythm to a tag* and *Rhythm tag
  name* - are gone with it rather than left doing nothing. All of it is
  commented out rather than deleted: the write in `rhythm_tag_or_empty`, the
  factories in `preferences.cpp` and their declarations in `globals.h`. An
  entry has no way to register itself and stay out of the preferences tree, so
  not registering it is what hiding it amounts to; the settings themselves are
  keyed by GUID in foobar2000's configuration and survive untouched, so
  restoring those lines restores the entries and their old values. The rhythm
  is still detected and still shown in the results window.
* The tempo a track opens at is measured, shown in the results window as
  *Initial BPM* and written to an `INITIALBPM` tag, named after the
  `INITIALKEY` other taggers write. It is the median of the first three
  autocorrelation windows - about the first 18 seconds, or a tango's
  introduction - quoted at the same metrical level as the BPM and scaled with
  it when a result is doubled or halved. On this repertoire it is a genuinely
  different number: the classic orchestras open a median 1.8 BPM above where
  they settle on shellac and 2.0 on vinyl, higher in eight sides of twelve
  either way, while the strict-tempo Orquesta Tipica Victor sides run the other
  way. On a synthesised ramp from 116 to 124 BPM it reads 117.4 where the ramp
  is at 116.6, so the figure leans toward the whole-track tempo and understates
  a real opening slightly - fitting a peak in a window whose tempo is moving
  does that, and the `tempo_spread` case pins the size of it.
* The results window shows how much the tempo moves over each track, as a
  plus-or-minus in BPM beside the BPM itself. The autocorrelation was already
  measuring the tempo of every 12-second window and throwing all but the median
  away; the figure is half the 10th-to-90th percentile span of those, so the
  middle 80% of a track sits inside it and a beatless introduction cannot set
  it. A synthesised metronome reads 0.07 and a linear ramp from 116 to 124 BPM
  reads 2.91 against the 2.88 the window geometry predicts, which is what the
  new `tempo_spread` test case checks. On the collection, milonga and vals read
  0.8 to 2.0, most tango sides 1.3 to 3.5, and Pugliese and Fresedo 3.9 to 6.5
  - which is the order a dancer would put them in. It costs nothing measurable.
* Windows the analysis could not track are dropped rather than counted at the
  edge of the search. The first attempt searched 15% either side of the settled
  beat and took whatever was best, which on a weak passage was whichever end
  the search stopped at; one Fresedo side came out at +/-14 BPM, an 11% swing,
  entirely from windows piled on the boundary. The search is 8% either side
  now - wider than any real drift, narrow enough to hold no competing
  periodicity - and a window whose autocorrelation is still climbing where the
  search ends is discarded. That side now reads 4.98 over the 27 windows that
  did track, which its trajectory bears out: about 133 BPM through the
  instrumental opening and 125 once the singer enters.
* `bpmcore_test trajectory` prints the tempo of each window, for asking why a
  particular track reads the way it does.
* Every column in the results window except the title is now sized to the
  widest string in it, header included, and the title takes what is left. The
  widths were hardcoded, and at 270 plus 50 plus 70 dialog units they already
  overflowed the 381-unit list slightly before a fourth column existed.
* An analysis now records itself in a `BpmAlgorithm` tag, written alongside
  the BPM as `Rubato;v=<version>` - the same field name shape and the same
  `<name>;v=<version>` value as the `KeyAlgorithm` and `TuningAlgorithm`
  fields other taggers write. Only a BPM the analysis stands behind gets it:
  tapping one by hand in the manual dialog, doubling or halving one from the
  context menu, and doubling or halving a result in the dialog before
  committing all remove the field instead, since the analysis no longer
  stands behind the value. Telling a measured BPM from a corrected or
  hand-tapped one no longer means guessing from whether it has a decimal
  point.
* The version has one home. It was written out twice - `project(VERSION)` in
  `CMakeLists.txt` and again in `DECLARE_COMPONENT_VERSION` - and the two had
  drifted, so 0.4.2 shipped an about box reading 0.4.1. CMake now generates a
  `version.h` from the project version, and the about box, the `BpmAlgorithm`
  tag and the release archive name all read it from there.
* Settings do not carry over, for the same reason. An existing BPM Analyser
  install keeps its own configuration and this one starts at its defaults, so
  the BPM tag name, the rhythm tag name and the STFT settings all need setting
  again if they were ever changed.

Earlier releases, as BPM Analyser
---------------------------------

### Version 0.4.2

* Files at 48kHz are analysed correctly. The onset envelope's geometry is fixed
  in seconds, which gets the window and hop right in time at any rate, but the
  window has to be a power of two as well - and at 48kHz that is 2048 points
  covering 42.7ms where the geometry asks for 46.4, with the six band edges on
  different bins. A 48kHz file and a 44.1kHz transfer of the same side were two
  different analyses, and could disagree on the metre and the rhythm. Anything
  whose rate is not 22.05kHz times a power of two is now resampled to 22.05kHz
  first, which is the rate the model was fitted at; 32kHz, 96kHz and 192kHz were
  wrong for the same reason and are fixed with it.
* The resampler is a rational polyphase FIR with a Kaiser-windowed sinc, 80dB of
  alias rejection and a passband flat to 0.01% out to 8kHz. It is cheap because
  the envelope never reads above 8kHz, so it only has to be clean from 14kHz up
  rather than from 11kHz up - six kilohertz of transition band instead of one.
* Audio is downmixed and resampled as the decoder produces it rather than
  afterwards, so what is held is bounded by the track's duration and not by its
  sample rate. A long file at 192kHz used to need 690MB of buffer and now needs
  79MB.
* Faster, with no change to any answer. The autocorrelation and the resampler
  are now spread across cores as the envelope already was, and all three give
  the same result at any thread count. For a 169-second track on all cores:
  22.05kHz 0.055s to 0.039s, 32kHz 0.103s to 0.059s, 48kHz 0.122s to 0.057s.
  48kHz is now quicker than 44.1kHz despite the extra stage, because it is
  analysed at 22.05kHz where the transform is a quarter of the size.
* Tracks are opened for a sequential read, so a decoder need not build a
  seektable that will never be used, and a file carrying looping metadata is no
  longer decoded round and round until the length cap stops it.
* With *output debug information* on, each track now logs how long it took to
  read and how long to analyse, as separate numbers. Reading is usually the
  larger of the two by a wide margin - a three-minute side is around 0.05s of
  analysis - so this is the first thing to look at if a scan feels slow.
* The transform size is now the nearest even number with no prime factor above 5
  rather than the nearest power of two. Every rate that reaches the analysis is
  resampled to one where those are the same value, so nothing measured changes;
  it keeps the window near 46.4ms on the one path left over, where a ratio the
  resampler cannot approximate means the track is analysed at its own rate.

### Version 0.4.1

* A tapped level is now only offered where the metre has one. Two beats is not
  a position in a 3/4 bar, and offering it sent slow valses - a Peruvian vals at
  56 to the bar - to the two-beat rate instead; vals goes from 90.5% to 94.1%
  within 2 BPM of the tap. For "other", which states no metre of its own, the
  level set follows the metre the grid search found, so a duple piece is no
  longer read at two thirds of its beat: *Chan Chan* was coming out at 112
  rather than 84, and *Guantanamera* at 83 rather than 125.
* Overall: within 2 BPM of the tap on 89.0% of 3,664 hand-tapped tracks, right
  metrical level on 96.6%.

### Version 0.4.0

* New tempo engine, aimed at Argentine tango. It reports the tempo on the
  metrical level a dancer taps - the beat for a tango, the bar for a vals or a
  milonga - which needs the rhythm, so the rhythm is detected first. Against
  3,664 hand-tapped tracks it lands within 2 BPM of the tap 88.5% of the time
  and picks the right metrical level for 96.2%; the previous behaviour of
  treating everything alike managed 66.9% within 2 BPM.
* New rhythm classifier: Tango, Vals, Milonga or other, from the audio alone,
  with no reference to the genre tag. 94.1% accurate over 12,118 tracks. The
  result is shown in the results dialog and written to a `RHYTHM` tag.
* The analysis is now a standalone library, `bpmcore`, with no foobar2000,
  Windows or ATL dependency, so it can be reused elsewhere and on other
  platforms. The component is a shell around it.
* Whole tracks are analysed rather than a few short excerpts, and local tempo is
  reduced with a median across overlapping windows, so a passage that drifts,
  a rubato phrase or a beatless introduction no longer moves the answer.
* Onset detection is now per frequency band. The old broadband flux was weighted
  towards the top of the spectrum, which on a shellac transfer is surface noise.
* The spectral stage is spread across cores, with a result that is identical
  whatever the thread count, and only the frequency bins the analysis actually
  uses leave the transform. A 48kHz file is about twice as fast as before for a
  second reason: its analysis window was being rounded up to 4096 points, which
  also made it a different analysis from the same track at 44.1kHz.
* The 2009 algorithm is still available: *Preferences > Advanced > Tools > BPM
  Analyser > Use the legacy BPM engine*. The preferences page's STFT and
  candidate-selection controls only apply to it.

### Version 0.3.0

* 64 bit support: the component now loads in 64 bit foobar2000 2.x. One
  .fb2k-component file carries both architectures.
* Ported from the 2011-03-11 SDK to the 2025-03-07 SDK, and from the Visual
  Studio 2010 project files to CMake.
* Fixed a crash analysing tracks whose decoder reports a sample rate below
  100Hz, or hands back less audio than one FFT window.
* Fixed the BPM tag name being read from the preferences page as ANSI into a
  fixed size buffer: non-ASCII tag names were mangled, and a long enough name
  overran the buffer.
* The "output debug information" preference now controls the per-track
  diagnostics that were previously compiled out.
* Peak picking sorts with std::sort rather than a bubble sort.

### Version 0.2.4.6

* Show tag progress window delayed
* Refactored tag writing for doubling and halving BPM tag

### Version 0.2.4.5

* Windows 7 taskbar indicates total track progress
* Fixed automatic tag writing

### Version 0.2.4.4

* Replaced FFTW with KISS FFT

### Version 0.2.4.3

* Preferences page fresh up
* Enabled dialog navigation in result and manual dialogs
* Introduced wrapper for FFTW to prepare replacement

### Version 0.2.4.2

* Fixed abort checks in worker thread
* Added basic exception handling in worker thread
* Numerous refactorings

### Version 0.2.4.1

* Numerous bug fixes

### Version 0.2.4

* Crash report fix (component about message)

### Version 0.2.3

* Updated to foobar2000 1.0 SDK
* Added double/halve buttons to results dialog
* Added option to auto write tags after analysis
* Limit preference range inputs
* Crash report fix (using info not yet cached)

### Version 0.2.2

* Crash report fix
* Candidate bpm selection can be mode, mean, median added to preferences
* Debug output added to preferences

### Version 0.2.1

* Miscellaneous bug fixes

### Version 0.2.0

* Added confirmation to rescan already tagged files
* Added ReplayGain style results dialog
* Added preferences page (with destination BPM tag)
* Added manual bpm calculation window

### Version 0.1.1

* Refactored code (with processing time decrease)
* Initial source code release

### Version 0.1.0

* Initial release
