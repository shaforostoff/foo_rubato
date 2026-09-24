Rubato BPM Analyzer for foobar2000
==================================

Detects the tempo of a track and which of Tango, Vals, Milonga it is - or none of the three - from the audio alone, without reading the genre tag.

The two answers are linked. The tempo a dancer taps is not a property of the
audio by itself: a tango is tapped on the beat, a vals once per 3/4 bar,
a milonga once per 2/4 bar, a reggae on the quarter note - under the skank rather
than on it. So the rhythm is settled first and the tempo reported on the level
that rhythm implies. Measured against 3,692 hand-tapped tracks the estimate
lands within 2 BPM of the tap 88.7% of the time, which is about as close as the
same person tapping the same track twice; rhythm classification is 93.6%
accurate. [docs/tango-analysis.md](docs/tango-analysis.md) has the method and
the full numbers, and [docs/cortina-genres.md](docs/cortina-genres.md) covers
the cortina genres that were measured and left alone.

Originally written by Michael Balzer+Holger Stenger as BPM Analyser,
but bpmcore engine was completely rewritten.

![Rubato BPM Analysis](/screenshot.png?raw=true)

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

`-Kiss` builds the same component on the portable double-precision transform
instead - the reference the shipping one is checked against, about 1.6x slower
over a whole analysis, and not what is shipped. `-Dynamic` links the C runtime
as a DLL rather than statically, which is 209KB off each built DLL and 111KB
off each packed, at the price of needing the Visual C++ redistributable on the
target machine.

Neither is the release build. Each uses its own build directory and puts its own
word in the archive name, and they compose - `-Kiss -Dynamic` writes
`foo_rubato-<version>-kiss-dynamic.fb2k-component` - so no switched build can
be mistaken for the shipping one. `cmake\fft_backend.cmake` and the
`FOO_RUBATO_*` options are what sit underneath them.

To work on it in Visual Studio, configure once and open the generated solution:

    cmake -S . -B build\x64 -A x64
    cmake --build build\x64 --config Release
    ctest --test-dir build\x64 -C Release

### macOS

    ./scripts/build_release_macos.sh

The Xcode command line tools and CMake are the only prerequisites. The script
fetches the SDK into `external/` on first run - WTL is not needed there - builds
one universal binary, runs the tests, pulls the debug info out into a `.dSYM`,
signs the bundle and writes

    dist/foo_rubato-<version>-mac.fb2k-component
      mac/foo_rubato.component    universal: Apple Silicon and Intel

foobar2000 for Mac 2.6 is the first release that loads third party components,
and `mac/` is where it looks. Given the archive the Windows script produced,
`--merge` puts both in one file:

    ./scripts/build_release_macos.sh --merge dist/foo_rubato-<version>.fb2k-component

    dist/foo_rubato-<version>.fb2k-component
      foo_rubato.dll              32 bit Windows
      x64/foo_rubato.dll          64 bit Windows
      mac/foo_rubato.component    macOS

which is one download that installs on either platform - each host takes the
folder it knows and ignores the rest. What it does not do is leave the rest
behind: foobar2000 unpacks the whole archive and then overwrites the root with
the subfolder it wants, so a Windows install carries the macOS bundle on disk
and a Mac install carries the DLLs. That costs half a megabyte either way and
nothing else, and it is how the scheme already works for `arm64ec\` - but it is
the reason to publish two archives instead if that matters.

The macOS payload is built here and the Windows one is carried through
untouched, so the merge is a packaging step and not a second build; it is safe
to point at the archive in `dist/` that it is about to write over, because
everything going into the new one is staged on disk before the old one is
touched.

The component is **ad-hoc signed** and that is all it needs. A Developer ID and
notarization are not required: foobar2000 for Mac runs under the hardened
runtime but ships `com.apple.security.cs.disable-library-validation`, which is
the entitlement that lets it load code signed by somebody else - or by nobody.
Signing at all is not optional, though. Apple Silicon will not map unsigned
code, and cross-building a universal binary does not sign it for you, so an
unsigned bundle fails to load on half the machines it is meant for. Pass
`-s "Developer ID Application: ..."` to use a real identity instead.

`--kiss` builds the same component on the portable double-precision transform,
as `-Kiss` does on Windows. There is no `--dynamic`: that selects between the
static and DLL Visual C++ runtimes, and there is no such choice to make against
the system libc++.

`-j` sets how many compile jobs run at once, and defaults to one fewer than the
machine has cores rather than to all of them. A job here is a clang holding a
translation unit with the foobar2000 SDK precompiled into it, which is large
enough that a machine with other work on it can run out of memory and have the
build killed - an exit code and nothing else. `-j 1` leaves a great deal more
room and is what to use on a machine that is already short.

    cmake -S . -B build/mac -DCMAKE_BUILD_TYPE=Release
    cmake --build build/mac
    ctest --test-dir build/mac

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
  Everything above the window system is one set of sources for both platforms;
  the window system is not. Windows gets WTL dialogs built from
  `foo_rubato.rc`, macOS gets Cocoa windows built in code under
  `foo_rubato/mac/`, and `foo_rubato/bpm_ui.h` is the line between them - two
  functions, one to put up the results window and one the tap window, which is
  all the rest of the component knows about either.
* `bpmcore_test/` verifies the analysis without foobar2000 running, and can
  benchmark and profile it.
* `foo_rubato_test/` checks the strings the component writes into files. It
  links pfc and `bpmcore` but not the foobar2000 SDK, which is what
  `bpm_key_format.h` is kept clear of the preferences page for.
* `scripts/analysis/` is the Python reference implementation and the training
  pipeline that generates `bpmcore/rhythm_model.h`. See its README.

### Settings

Everything the analysis needs is fixed by the model it was fitted to, so what
is left on the preferences page is what a user would actually choose:

* **Tagging** - the BPM precision, the BPM tag name, whether to write tags
  without showing the results window, whether to write `INITIALBPM` beside the
  BPM, and whether to write the `BpmAlgorithm` and `KeyAlgorithm` attributions
  at all. The last two are on by default.
* **Tuning and key** - whether to measure the tuning offset and the key at
  all, and which of `KEY`, `TUNING` and `RETUNE` to write. All four are on by
  default. Turning detection off greys the other three and saves about half
  the analysis, which on a library scan is a small fraction of the decode.
* **Manual Analysis** - taps to average, and how long a pause resets the
  average, for the tapping dialog.
* **Diagnostics** - whether each track's analysis goes to the console.

There is nothing under **Preferences > Advanced > Tools** any more. Everything
that was there has gone: the legacy-engine switch with the engine itself, and
the two rhythm-tag entries with the tag writing, which is commented out in
`bpm_rhythm_tag_or_empty` in `bpm_result_format.h`. An advanced-config entry
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
which is the only place the version is written down. The key carries its own
attribution under that same `KeyAlgorithm` name, on the same terms; it is in
*Tuning and key* below, because what it stands behind is measured there.

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
uncheck *Write BpmAlgorithm and KeyAlgorithm*, tap a BPM by hand over one that
was measured, and the old attribution still goes, because the alternative is a
file saying the analysis produced a number the user typed. The same holds for
`INITIALBPM`, where a stale value would describe a different measurement from
the BPM beside it. What the switches do cost is the reverse inference: with the
attribution off, a missing `BpmAlgorithm` no longer means the BPM was tapped,
only that nothing wrote one.

`INITIALBPM` follows the BPM rather than the attribution, because it is a
measurement and not a claim about who made it: doubling or halving scales it,
since a BPM read at the wrong metrical level had its opening read at the wrong
level too and one factor puts both right. A hand-tapped BPM removes it, there
being no opening tempo in a tap. Like `BpmAlgorithm` the name is fixed rather
than configurable.

Where the BPM itself lands depends on the container. In an mp3 it is the
standard `TBPM` frame and in FLAC or Ogg the `BPM` comment, both of which
players read. In an m4a foobar2000 writes it as a freeform `BPM` atom rather
than the `tmpo` atom players look for, and no field name makes it write
`tmpo`. So the component writes `tmpo` itself once foobar2000 has finished,
rounded to a whole number, which is all the atom holds. It overwrites an
existing `tmpo` in place, and adds one only where foobar2000's own padding
behind the tags has room for it, so the audio is never moved; a file laid out
any other way is left alone and says so in the console.

### Tuning and key

Two more measurements come off the same decode, and they are written as eight
fields rather than two because neither is certain enough to state as a fact:

    KEY                Dm
    INITIALKEY         Dm       (TKEY in an mp3, "initialkey" in an m4a)
    KEYCANDIDATES      Dm:0.866 C:0.702 Gm:0.701
    KEYCONFIDENCE      high
    MODEBALANCE        40% major, 7 switches
    TUNING             -19.8
    RETUNE             +0.00% to A=435
    RETUNECANDIDATES   +0.00%@A=435 +1.15%@A=440

`KEY` is the single best guess, so that a player or a DJ tool sees something
usable. It is right about 79% of the time. `KEYCANDIDATES` is where the rest
of what was measured is: the true key is somewhere in those three 95% of the
time. `KEYCONFIDENCE` says which of those numbers applies - in the `high` band
the single answer is right 89% of the time, in `medium` and `low` 64%. The three travel
together or none of them is written, because `KEY` on its own reads as a fact
and it is not one.

The key goes to the container's standard key slot as well as to `KEY`,
because that slot is the one players and DJ tools read and `KEY` reaches
almost none of them. foobar2000 only maps one name onto a native key frame -
`INITIAL KEY`, with the space, becomes `TKEY` in an mp3 - and writes every
other name as it is given, so the component picks the name per container:
`INITIAL KEY` for mp3, a lower-case `initialkey` for m4a (the spelling beaTunes
and Mixed In Key use), and `INITIALKEY` for FLAC, Ogg and everything else. The
slot is shared with beaTunes, and a scan replaces beaTunes' key there; with
the key switched off the slot is cleared only if `KeyAlgorithm` says this
component wrote it.

`MODEBALANCE` is which of the relative pair was in charge and how often it
changed hands. That is not a key change: a tango with a minor A section and a
major B section keeps one key signature throughout.

Two more fields record attribution rather than a measurement:

    KeyAlgorithm    = Rubato;v=<version>
    TuningAlgorithm = Rubato;v=<version>

the same value as `BpmAlgorithm`, one component and one build stamping both,
and under the name other taggers already use for it. There are two fields
because the two measurements are overruled separately - doubling a BPM in the
results window says nothing about the key beside it, and a track with no
steady pitch in it still produced a BPM worth attributing, so one attribution
covering both would be wrong about one of them every time either is. What
`KeyAlgorithm` stands behind is specifically a `KEY` this scan wrote: where
none was measured, or where the key fields are switched off, it is removed
along with them rather than left claiming credit for a field that is not there
- unless another tagger's name is in it, in which case its key and its
attribution are both left alone. The attributions share the *Write
BpmAlgorithm and KeyAlgorithm* checkbox under **Tagging**.

`TUNING` is cents from A=440, the field beaTunes also writes. foobar2000
treats beaTunes' `Tuning` and this `TUNING` as one field, so a scan replaces
beaTunes' value, and `TuningAlgorithm` follows it so the file does not go on
crediting beaTunes for our number. It is a good deal more reliable than the key -
against 130 transfers whose speed TangoTunes set by hand the median error is
about 2 cents, and which of A=435 and A=440 the transfer was made at comes
back from the audio alone 98% of the time.

`RETUNE` is the speed correction that would put the side back on pitch, and it
needs the recording year, which no amount of signal processing supplies: the
component reads `ORIGINALDATE` first and falls back to `DATE`. A measured
offset is only known modulo a semitone, and through the 1939-1944 transition
both reference pitches were in use, so `RETUNECANDIDATES` carries the
alternatives when there is more than one. Nothing is suggested for a track
with no year, or one recorded from 1976 on.

All of this is off one switch on the preferences page, and each of the three
groups can be turned off separately. As with `BpmAlgorithm`, turning a field
off stops this component adding one and does not let a stale value stand: a
rescan that measures nothing removes the fields rather than leaving the last
scan's answer behind. A BPM tapped by hand leaves them alone entirely - a tap
says nothing about the key, and a measurement already on the file is still
true.

How all of this was derived and measured, and the eight approaches that were
tried and rejected along the way, is in `key-detection-feature-plan.md`.

### How the build hangs together

* `scripts\get_sdk.ps1` downloads the SDK and WTL, checks both against a pinned
  SHA256, and unpacks them into `external\`. CMake runs it by itself when they
  are missing, so a fresh checkout needs no manual setup. WTL is a separate
  download because the SDK's helpers include `<atlapp.h>` but do not ship it;
  ATL itself comes with Visual Studio.
* `scripts/get_sdk.sh` is the macOS counterpart, and fetches the SDK alone.
  The release, the URL and the checksum are not repeated in it: it reads them
  out of `get_sdk.ps1`, which stays their one home. Two pins that have to be
  updated together are two pins that will not be, and the about box states the
  SDK release it was built against from that same file - so a Mac build
  fetching a different one would make the about box wrong on one platform and
  right on the other, with nothing to say which.
* `cmake\fb2k_sdk.cmake` builds the SDK from source as static libraries behind
  the `fb2k::sdk` target. This component needs the whole stack rather than the
  SDK core alone, because it has dialogs, a preferences page and a
  preferences-backed tag writer. What the stack is differs by platform: on
  Windows, pfc, the SDK proper, libPPUI and helpers, with `shared.dll` linked
  through its import library because it ships with foobar2000 itself. On macOS,
  pfc, the SDK proper, the part of helpers that is not Win32, and `shared`
  built from source - there is no dylib to import against. libPPUI does not
  come along: it is Win32 window classes from top to bottom and the SDK ships
  no Xcode project for it. Which files each platform compiles is not guesswork
  - the exclusion lists are the difference between what is in each directory
  and what the SDK's own Xcode projects build.
* `kiss_fft` and `pffft` are both vendored, and both are always built. Which
  one `bpmcore` links, and at what width, is `cmake/fft_backend.cmake`'s
  decision - `-DBPMCORE_FFT_BACKEND=pffft|kiss` and
  `-DBPMCORE_FFT_SCALAR=float|double`. pffft is about six times faster at these
  sizes and is single precision only; kiss is portable scalar C and builds
  anywhere. **The default is pffft at float, which is what the component
  ships**; naming kiss alone gives double, because the reference build is both
  of those things at once. What the narrowing costs was measured over the whole
  collection: of 12,157 tracks analysed by both, none changed its rhythm, its
  meter or its metrical level.
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
  `dialog_labels`. Windows only: there are no dialog templates in the macOS
  component for it to read, its windows being built in code, where a label that
  does not fit is a layout constraint rather than a resource.

`build\`, `external\` and `dist\` are all ignored by git, under either
spelling.

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
transform - pffft by default, or KISS FFT where portability matters more than
speed; there is no foobar2000, Windows or ATL in it.

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
