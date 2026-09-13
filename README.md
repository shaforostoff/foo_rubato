Rubato BPM Analyzer for foobar2000
==================================

Detects the tempo of a track and which of Tango, Vals, Milonga it is
- or none of the three - from the audio alone, without reading the genre tag.

The two answers are linked. The tempo a dancer taps is not a property of the
audio by itself: a tango is tapped on the beat, a vals once per 3/4 bar, a
milonga once per 2/4 bar, a reggae on the quarter note - under the skank rather
than on it. So the rhythm is settled first and the tempo reported on the level
that rhythm implies. Measured against 3,692 hand-tapped tracks the estimate
lands within 2 BPM of the tap 88.7% of the time, which is about as close as the
same person tapping the same track twice; rhythm classification is 93.6%
accurate. [docs/tango-analysis.md](docs/tango-analysis.md) has the method and
the full numbers, and [docs/cortina-genres.md](docs/cortina-genres.md) covers
the cortina genres that were measured and left alone.

Originally written by Michael Balzer+Holger Stenger as BPM Analyser,
but bpmcore engine was completely rewritten.


Building
--------

    .\scripts\build_release.ps1

Visual Studio with the C++ workload, and CMake, are the only prerequisites. The
script fetches the foobar2000 SDK and WTL into `external\` on first run, builds
both architectures, runs the tests and writes

    dist\foo_rubato-<version>.fb2k-component
      foo_rubato.dll        32 bit, foobar2000 1.x and 2.x (x86)
      x64/foo_rubato.dll    64 bit, foobar2000 2.x (x64)

foobar2000 ignores subfolders it does not understand, so that single file
installs everywhere. Symbols are packaged separately as
`dist\foo_rubato-<version>-symbols.zip`; keep them so crash reports can be
resolved, but do not ship them.

`-Pffft` builds the same component on the faster single-precision transform
instead - about 1.7x on a whole analysis, and not what is shipped. `-Dynamic`
links the C runtime as a DLL rather than statically, which is 209KB off each
built DLL and 111KB off each packed, at the price of needing the Visual C++
redistributable on the target machine.

Neither is the release build. Each uses its own build directory and puts its own
word in the archive name, and they compose - `-Pffft -Dynamic` writes
`foo_rubato-<version>-pffft-dynamic.fb2k-component` - so no switched build can
be mistaken for the shipping one. `cmake\fft_backend.cmake` and the
`FOO_RUBATO_*` options are what sit underneath them.

To work on it in Visual Studio, configure once and open the generated solution:

    cmake -S . -B build\x64 -A x64
    cmake --build build\x64 --config Release
    ctest --test-dir build\x64 -C Release

### Layout

* `bpmcore/` is the analysis, and has no host in it - no foobar2000, no pfc, no
  ATL, no `windows.h`. Only the standard library and one FFT, reached through
  `bpmcore/real_fft.h` so that no other file names a transform, so the same
  sources build for a command line tool, a macOS host or an Android target.
  Start at
  `bpmcore/bpmcore.h`. The three stages that are worth spreading across cores -
  resampling, the envelope and the autocorrelation - all divide their work so
  that the answer does not depend on the thread count.
* `foo_rubato/` is the foobar2000 component: decoding, tag writing, dialogs and
  preferences. It hands `bpmcore` mono PCM and gets a tempo and a rhythm back.
* `bpmcore_test/` verifies the analysis without foobar2000 running, and can
  benchmark and profile it.
* `scripts/analysis/` is the Python reference implementation and the training
  pipeline that generates `bpmcore/rhythm_model.h`. See its README.

### Settings

Everything the analysis needs is fixed by the model it was fitted to, so what
is left on the preferences page is what a user would actually choose:

* **Tagging** - the BPM precision, the BPM tag name, whether to write tags
  without showing the results window, and whether to write `INITIALBPM` and
  `BpmAlgorithm` beside the BPM. The last two are on by default.
* **Manual Analysis** - taps to average, and how long a pause resets the
  average, for the tapping dialog.
* **Diagnostics** - whether each track's analysis goes to the console.

There is nothing under **Preferences > Advanced > Tools** any more. Everything
that was there has gone: the legacy-engine switch with the engine itself, and
the two rhythm-tag entries with the tag writing, which is commented out in
`rhythm_tag_or_empty` in `bpm_result_dialog.cpp`. An advanced-config entry
cannot register itself and stay out of the tree, so hiding one means not
registering it; those are commented out in `preferences.cpp` along with the
branch, and restoring all three places brings them back with their old values,
which persist in foobar2000's configuration keyed by GUID either way. The
rhythm is still detected and still shown in the results window.

The nine STFT and candidate-selection controls that used to fill an *Automatic
Analysis* group are gone with the engine that read them. That was the original
2009 algorithm, kept switchable through the 2025 port and removed in 0.1.0: it
was off by default, unreachable without the advanced switch, untested - both
harnesses link `bpmcore` alone, and it lived in the component - and it filled
in only a BPM, so it left the rhythm reading "Other" for every track and had
its results stamped `BpmAlgorithm=Rubato`, which was not true. It is in git
history if it is ever wanted.

### Tempo fluctuation

The results window carries a **Fluctuation** column: how far the tempo moves
over the track, as a plus-or-minus in BPM at the level the BPM column shows.

The autocorrelation already runs over 12-second windows on a 3-second hop and
reduces them with a median, so the tempo of each window is there to be read
rather than needing a second pass; each window is asked for its own beat period
near the settled one, and the figure is half the span between the 10th and 90th
percentile of those. So the middle 80% of the track sits within the quoted
figure of the middle, and one badly tracked window or a beatless introduction
cannot set it. It costs nothing measurable - a 138-second track is still 0.03s
of analysis.

A window whose autocorrelation is still climbing where the search stops is
discarded rather than counted at the edge, which is what separates a tempo that
moves from a track the windows could not follow. Below seven usable windows -
about half a minute of audio - the column is left blank instead of drawn from
two or three.

What the numbers look like, on 3,664 hand-tapped tracks' worth of collection:

| | fluctuation |
|---|---|
| synthetic metronome | 0.07 |
| milonga, vals - the steady rhythms | 0.8 - 2.0 |
| most tango sides | 1.3 - 3.5 |
| Pugliese, Fresedo - the rubato orchestras | 3.9 - 6.5 |

A real performance never reads zero: a metronome does, but human playing has a
BPM or so of genuine give in it before any measurement error. Two limits are
worth knowing. It is a floor on the real variation, not a full account of it -
a wobble that finishes well inside 12 seconds is averaged away rather than
seen. And it cannot tell a performance that speeds up from a transfer running
fast, because both move the beat period the same way; `bpmcore_test
trajectory` prints the per-window tempo, where a drifting transfer walks in one
direction and a performance breathes.

### Committing the results

The results window writes nothing until *Update N files* is clicked, and then
it writes every track in the list. The selection is for the double and halve
buttons, which act on the rows highlighted; it does not narrow what gets
written, which is why the button counts the files rather than saying *Update
files* and leaving the question open.

*Cancel* closes the window and writes nothing. *Write tags automatically*
skips the window altogether and writes everything the scan produced.

### Comparing against what was already there

When at least one of the scanned tracks arrives with a BPM tag already on it,
the results window grows a **BPM from tag** column showing what the file said,
next to the BPM just measured. On this collection those existing values are
hand taps, so the column is the measurement set against the tap it should be
judged by.

It is shown exactly as the file carried it, decimal point included, because on
this collection a whole number is a hand tap and a decimal is machine-written -
a distinction worth more than a tidy column. The column is absent entirely when
no track had a tag, rather than sitting there empty.

Every track in the selection reaches the window. It used to be that one track
without a BPM tag caused every track that had one to be dropped, without a
word - so a selection of twenty could come back as a single row, and this
column was unfillable in the one case that most wanted it. Analysing writes
nothing to the files on its own, so there is nothing there to protect. The one
question the component asks is when *Write tags automatically* is on and some
of the selection already carry a tag, because then the numbers go straight to
the files and nothing is shown first: it offers to scan all of them, to scan
only the untagged ones, or to stop.

### The tempo a track opens at

The **Initial BPM** column, and the `INITIALBPM` tag, are the tempo at the
start rather than over the whole side - the median of the first three
autocorrelation windows, which overlap to cover about the first 18 seconds,
roughly a tango's introduction. Quoted at the same metrical level as the BPM
beside it, and scaled with it when a result is doubled or halved.

It is a different question from the BPM, and on this repertoire it has a
different answer often enough to be worth a column. Sampling the collection,
the classic orchestras open faster than they settle - a median of +1.8 BPM on
shellac and +2.0 on vinyl, higher in eight sides of twelve either way, which is
the orchestra easing off as the singer comes in. The Orquesta Tipica Victor
sides run the other way, which is what a dance orchestra cut to a strict tempo
should do.

The figure is conservative on a side that moves. Fitting a peak in a window
whose tempo is changing pulls the estimate toward the tempo of the whole track:
on a synthesised ramp from 116 to 124 BPM the opening reads 117.4 where the
ramp is at 116.6, so a real opening is a little further from the overall figure
than the column says. Where the start of a track has no beat to measure - a
rubato introduction, a spoken opening - the first windows that do have one are
used, and if none do the column is blank.

### Scanning a lot of tracks

Tracks are scanned several at a time, two short of what the machine reports -
`std::thread::hardware_concurrency() - 2`, and never fewer than one. Decoding a
track is a solid block of one core and dwarfs the analysis that follows it: a
138-second side is about a tenth of a second of analysis against seconds of
decoding on a compressed format. Reading more than one track at once is the
only thing that makes a library scan faster.

Two cores are left alone because a scan is something a DJ starts in the middle
of a set. Every scanning thread holds a decoder flat out, and foobar2000's own
playback decode and its user interface want a core between them. The scanning
threads also run at below-normal priority, and that is the part which actually
protects playback: on a machine with hyperthreading `hardware_concurrency`
counts logical processors, so subtracting two leaves two hyperthreads rather
than two cores, and every physical core is in use either way. Priority is what
settles who waits when they are.

Each scan keeps its own analysis on its own thread, rather than spreading the
spectral stage across the machine as `bpmcore` does when left to decide, which
would be the same cores counted twice. A single track has nothing to share with
and gets the machine. The answer is identical either way, and
`bpmcore_test tempo_spread` checks that it is - bit for bit, for one thread,
two and all of them - which is what makes the thread count safe to vary at all.

Peak memory is worth knowing before scanning a whole library. Audio is buffered
rather than streamed, because the onset envelope has to be normalised by the
track's overall level and that is not known until the side has been read, so
each scanning thread holds about 21MB for a track of ordinary length and up to
79MB for one at the 15-minute cap. On sixteen logical processors that is around
290MB for tango sides, and over a gigabyte if every slot happens to hold a very
long file.

Progress reporting stays on one thread: the one `threaded_process` started,
which does no scanning itself. `threaded_process_status` is handed to that
thread and nothing in the SDK promises it is safe from several at once, so it
reads what the scanning threads publish instead. It shows the tracks in flight
as *a.flac, b.flac and 4 more*, and puts the average of their progress on the
second bar - the only reading of a single bar that means anything with several
tracks under it.

### Tags

An automatic analysis writes the BPM to the tag named on the preferences page,
`BPM` by default; the tempo the track opens at to `INITIALBPM`; and

    BpmAlgorithm = Rubato;v=<version>

which records what produced the number. Both the field name and the
`<name>;v=<version>` shape follow the `KeyAlgorithm` and `TuningAlgorithm`
fields other taggers write, so one parser reads all three. It is not
configurable - a reader looking for an attribution has to know what it is
called - and the version comes from `project(VERSION)` in `CMakeLists.txt`,
which is the only place the version is written down.

Only a BPM the analysis stands behind is stamped. All three ways of overruling
it *remove* the field instead - tapping a BPM by hand in the manual dialog,
doubling or halving one from the context menu, and doubling or halving a
result with the results dialog's own buttons before committing. An attribution
left over from an earlier scan would otherwise be claiming credit for a number
the analysis did not produce. So the presence of the field is a reliable way to
tell a measured BPM from a corrected or hand-tapped one.

Both fields can be turned off under **Tagging** on the preferences page, and
both are on by default. Off means this component stops *adding* the field - not
that it starts leaving a claim it knows to be false. Removal is unconditional:
uncheck *Write BpmAlgorithm*, tap a BPM by hand over one that was measured, and
the old attribution still goes, because the alternative is a file saying the
analysis produced a number the user typed. The same holds for `INITIALBPM`,
where a stale value would describe a different measurement from the BPM beside
it. What the switches do cost is the reverse inference: with *Write
BpmAlgorithm* off, a missing attribution no longer means the BPM was tapped,
only that nothing wrote one.

`INITIALBPM` follows the BPM rather than the attribution, because it is a
measurement and not a claim about who made it: doubling or halving scales it,
since a BPM read at the wrong metrical level had its opening read at the wrong
level too and one factor puts both right. A hand-tapped BPM removes it, there
being no opening tempo in a tap. Like `BpmAlgorithm` the name is fixed rather
than configurable, and foobar2000 picks the spelling each container wants -
`INITIALKEY`, the field it is named after, is upper case in a Vorbis comment,
lower case in an iTunes freeform atom and the standard `TKEY` frame in ID3.

### How the build hangs together

* `scripts\get_sdk.ps1` downloads the SDK and WTL, checks both against a pinned
  SHA256, and unpacks them into `external\`. CMake runs it by itself when they
  are missing, so a fresh checkout needs no manual setup. WTL is a separate
  download because the SDK's helpers include `<atlapp.h>` but do not ship it;
  ATL itself comes with Visual Studio.
* `cmake\fb2k_sdk.cmake` builds the SDK from source as four static libraries -
  pfc, the SDK proper, libPPUI and helpers - behind the `fb2k::sdk` target.
  This component needs the whole stack rather than the SDK core alone, because
  it has dialogs, a preferences page and a preferences-backed tag writer.
* `kiss_fft` and `pffft` are both vendored, and both are always built. Which
  one `bpmcore` links, and at what width, is `cmake/fft_backend.cmake`'s
  decision - `-DBPMCORE_FFT_BACKEND=kiss|pffft` and
  `-DBPMCORE_FFT_SCALAR=double|float`. pffft is about six times faster at these
  sizes and is single precision only; kiss is portable scalar C and builds
  anywhere. The default is kiss at double, which is what the component ships.
* `kiss_fft_test` checks the vendored library against stored reference
  spectra, and is wired into CTest. It existed for the half-complex packing the
  legacy engine's FFT wrapper performed; that wrapper is gone and `bpmcore`
  reads the bins directly, so what is left guards the dependency `bpmcore` does
  still have.
* `fft_backend_test` checks pffft against kiss at every size `bpmcore` can ask
  for. That is what makes the fast transform safe to swap in, and it is the
  thing to run first on a new architecture - pffft falls back to scalar code
  silently when it finds no SIMD, and this prints the width it settled on.
* `bpmcore_test` checks that the decision trees compiled into
  `bpmcore/rhythm_model.h` still reproduce the classifier they were exported
  from, and is wired into CTest too. It also runs the analysis over raw PCM:

      bpmcore_test pipeline track.f32 44100      # tempo and rhythm
      bpmcore_test bench    track.f32 44100 5    # timing
      bpmcore_test profile  track.f32 44100 5    # timing per stage
      bpmcore_test resample                      # resampler, and rate independence

  The `resample` case synthesises its own audio, so it runs in CI with no
  collection to hand. It is also wired into CTest.
* `dialog_test` draws the dialog templates out of the built DLL and checks that
  every label fits the control around it - see below. Wired into CTest as
  `dialog_labels`.

`build\`, `external\` and `dist\` are all ignored by git.

### Looking at the dialogs without foobar2000

The preferences page, the results window and the manual tap dialog cannot be
opened without foobar2000, and for a long time they were only ever compiled.
Two layout faults reached a release that way: a results window whose columns
were sized by guesswork, and three checkboxes carrying `BS_CENTER`, which sets
a short label adrift in the middle of its control instead of against the box.
Neither is visible in the `.rc` file, and both are obvious the moment the thing
is drawn.

A dialog template is only a resource, though, and the dialog manager will build
one from any process. So they can be drawn from the built DLL alone:

    .\scripts\render_dialogs.ps1              # one PNG per dialog, in build\x64\dialogs\
    .\scripts\render_dialogs.ps1 -Arch x86 -Show

The label check runs without the images as the `dialog_labels` CTest case, so
`build_release.ps1` catches a clipped label on its own. It reports what each
label needs against what its control has, in pixels, and exits 77 - which CTest
reads as a skip - where there is no desktop to draw on.

What it cannot show is anything the component fills in at run time: the results
window's columns and rows, the combo box items, the contents of edit controls,
which check is set. Geometry and the text baked into the template are what is
being looked at, which is where both of those faults lived.

`-Font` redraws with a face of your choosing, to ask what a host restyling the
page would do. Use it knowing what it means. The dialog manager derives dialog
units from the template's own `FONT`, so every control rectangle is already
expressed in terms of that face; pushing a wider one onto controls sized from a
narrower one reports healthy labels as clipped. That mistake cost an hour, and
the flag is kept mostly to make it namable.

Using the analysis elsewhere
----------------------------

`bpmcore` is a static library with one public header. Its only dependency is a
transform - KISS FFT by default, or pffft where speed matters more than
portability; there is no foobar2000, Windows or ATL in it.

```cpp
#include <bpmcore/bpmcore.h>

bpmcore::collector c(sample_rate);
while (decode(...)) c.add_interleaved(buffer, frames, channels);

const bpmcore::analysis a = c.finish();
// a.bpm, a.rhythm, a.confidence, a.beat_bpm, a.meter
```

The analysis runs at 22.05kHz, the rate the rhythm model was fitted at. Any
input rate that does not reproduce that geometry exactly is resampled on the way
in, so a 48kHz file and a 44.1kHz transfer of the same side give the same answer
and a 192kHz file costs no more to collect than a 44.1kHz one.

`analyse()` takes mono PCM directly if the caller already has it. Both float and
double samples are accepted. Pass a `bpmcore::listener` for progress and
cancellation, and a `bpmcore::options` to control threading - the spectral stage
is over 90% of the run time and is spread across cores by default, with an
answer that does not depend on the thread count.

Links
-----

* [foobar2000 home page](http://www.foobar2000.org/)
* [BPM Analyser](http://www.hydrogenaudio.org/forums/index.php?showtopic=77142),
  the original component, on the foobar2000 forum
* [KISS FFT](http://sourceforge.net/projects/kissfft/)
* [PFFFT](https://bitbucket.org/jpommier/pffft)
