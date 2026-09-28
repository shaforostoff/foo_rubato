Rubato BPM Analyzer for DeaDBeeF
================================

The same analysis as the foobar2000 component - `bpmcore`, unchanged - as a
[DeaDBeeF](https://deadbeef.sourceforge.io/) plugin. It measures the tempo,
the rhythm (Tango, Vals, Milonga, Reggae or none of them), the tuning and the
key of the selected tracks, and writes them to the files under the same field
names and the same rules as foo_rubato, so a library tagged by either player
reads the same in the other.

It needs DeaDBeeF 1.8.0 or later (plugin API 1.10), and builds for Windows,
macOS and Linux. It has foo_rubato's results window, a progress window and the
tapping window - in GTK 3 for DeaDBeeF's GTK 3 interface on Linux and Windows,
and in Cocoa for DeaDBeeF for Mac. Built without them, it writes as soon as a
scan finishes and reports in the log. See [Windows](#windows) for how the two
toolkits share them.

Building
--------

A C++17 compiler and CMake 3.16 or later, and for the GTK windows the GTK 3
development files. Nothing is downloaded: DeaDBeeF's plugin headers are in
`include/`, and `bpmcore`, PFFFT and KISS FFT are built from the directories
beside this one. Run these from the repository root.

`-DRUBATO_DDB_GTK=ON` or `OFF` decides whether the GTK windows are built. It
is on by default on Linux and with MinGW, where DeaDBeeF's interface is GTK 3,
and configuring fails with instructions if GTK 3.10 or later is not found; it
is off by default with MSVC and on macOS. `-DRUBATO_DDB_COCOA=ON` or `OFF` does
the same for the Cocoa windows, which need nothing but the macOS SDK; it is on
by default on macOS and available nowhere else. A build has one or the other.

### Windows, Visual Studio

    cmake -S deadbeef_rubato -B build\ddb-x64 -A x64
    cmake --build build\ddb-x64 --config Release
    ctest --test-dir build\ddb-x64 -C Release

writes `build\ddb-x64\Release\ddb_rubato.dll`, without windows. The C
runtime is linked statically, so it needs no Visual C++ redistributable. DeaDBeeF itself is
built with MinGW, which does not matter: the plugin interface is a table of C
function pointers and nothing allocated on one side is freed on the other.
`deadbeef.h` includes two POSIX headers MSVC does not have; `compat/msvc/`
stands in for them and is on the include path for MSVC builds only.

Build `-A x64` for the 64-bit DeaDBeeF, which is the one its site offers; a
plugin loads only into a player of its own architecture.

For the windows on Windows, use MSYS2 below instead. The windows have to draw
with the GTK DLLs DeaDBeeF ships, which come from MSYS2's MINGW64 environment,
and an MSVC build cannot link against those.

### Windows, MSYS2 MinGW

From a MINGW64 shell - the environment DeaDBeeF's own Windows build uses -
with `mingw-w64-x86_64-toolchain`, `mingw-w64-x86_64-cmake`,
`mingw-w64-x86_64-ninja` and `mingw-w64-x86_64-gtk3` installed:

    cmake -S deadbeef_rubato -B build/ddb-mingw -G Ninja -DCMAKE_BUILD_TYPE=Release
    cmake --build build/ddb-mingw
    ctest --test-dir build/ddb-mingw

libstdc++ and libgcc are linked in statically, so the one DLL is all there is
to copy; GTK is not, because the windows must use the copy DeaDBeeF has
already loaded. With `-DRUBATO_DDB_GTK=OFF` winpthreads goes in as well and
the DLL depends on nothing but Windows.

### macOS

    cmake -S deadbeef_rubato -B build/ddb-mac -DCMAKE_BUILD_TYPE=Release
    cmake --build build/ddb-mac
    ctest --test-dir build/ddb-mac

writes `build/ddb-mac/ddb_rubato.dylib`, universal (Apple Silicon and Intel),
ad-hoc signed, with the Cocoa windows. It runs on macOS 10.13 on Intel, which
is what DeaDBeeF for Mac asks for, and 11 on Apple Silicon. The signature is
not optional - Apple Silicon will not load unsigned code - but it need not be
a real one, because DeaDBeeF does not run under the hardened runtime.
`-DRUBATO_CODESIGN_IDENTITY="Developer ID Application: ..."` signs with a real
identity instead, and `-DCMAKE_OSX_ARCHITECTURES=arm64` builds for one
architecture only.

For a release, `scripts/build_release_deadbeef_macos.sh` builds it universal,
runs the tests, moves the debug information into a `.dSYM`, strips and signs
the library again, checks that both architectures are in it, that it exports
only its entry point, links to nothing but macOS itself and loads in every
architecture the machine can run, and makes an installer of it,
`dist/ddb_rubato-<version>-macos-universal.pkg` - see [Installing](#installing).
The installer is unsigned unless `--sign` names a "Developer ID Installer"
identity, which also has the library signed by its "Developer ID Application"
partner (or by `--codesign`); `--notarize --notary-profile <name>` then has it
notarized and the ticket stapled, and only a signed and notarized installer
opens from a double-click once downloaded. `--install` also copies the library straight to
where the installer puts it, to try it. `--help` lists the options.

### Linux

    cmake -S deadbeef_rubato -B build/ddb -DCMAKE_BUILD_TYPE=Release
    cmake --build build/ddb
    ctest --test-dir build/ddb

writes `build/ddb/ddb_rubato.so`. The GTK 3 development files are
`libgtk-3-dev` on Debian and Ubuntu, `gtk3-devel` on Fedora.

For a release, `scripts/build_release_deadbeef_linux.sh` does the same with
the C++ runtime linked in, runs the tests, checks that the library loads and
exports only its entry point, splits off the debug symbols and packs
`dist/ddb_rubato-<version>-linux-x86_64.zip` with the library under
`plugins/`, as DeaDBeeF's plugin builder packs the plugins it distributes. It
builds with this machine's toolchain, so the plugin needs a glibc at least as
new as this machine's; the script says which at the end. Build on the oldest
distro the release is meant for. `--help` lists the options.

### DeaDBeeF's plugin builder

DeaDBeeF's own downloads page is built by
[deadbeef-plugin-builder](https://github.com/DeaDBeeF-Player/deadbeef-plugin-builder),
which runs GNU Make or autotools and not CMake, so `Makefile` builds the
plugin for it - the shipping configuration, from the same sources, with the
compiler and flags the builder sets. It has to be kept in step with
`CMakeLists.txt` by hand. `make check` builds and runs the host test against
the same objects. The builder compiles against GTK 3.10, which is why the GTK
windows use nothing newer.

The builder's side is `plugins/ddb_rubato/manifest.json` in its repository.
It builds on Linux in an Ubuntu 20.04 container, which is what makes that
package run on glibc 2.31 and later; run locally, its `build` tool uses the
local glibc like everything else.

### Options

`-DBPMCORE_FFT_BACKEND=kiss` builds on the portable double-precision
transform, as `-Kiss` does for the foobar2000 component; the default is PFFFT
at float, which is what the component ships. `-DRUBATO_DDB_BUILD_TESTS=OFF`
leaves the tests out.

Installing
----------

Copy the library, under exactly this name, to

| | |
|---|---|
| Windows | `%APPDATA%\deadbeef\plugins\ddb_rubato.dll`, or `plugins\` beside `deadbeef.exe` in a portable install |
| macOS   | `~/Library/Application Support/Deadbeef/Plugins/ddb_rubato.dylib` |
| Linux   | `~/.local/lib/deadbeef/ddb_rubato.so` |

and restart DeaDBeeF. The name matters: DeaDBeeF finds a plugin's entry point
by taking the file name, dropping the extension and appending `_load`, so a
renamed library is skipped without a word. *Rubato BPM Analyzer* should then
be listed under **Preferences > Plugins**.

On macOS, the release is an installer, `ddb_rubato-<version>-macos-universal.pkg`,
that does this: it installs for the current user only, with no administrator
password, into the folder above, and asks for DeaDBeeF to be quit first if it
is running. If it is not signed and notarized, macOS will not open it from a
double-click once it has been downloaded; right-click it and choose **Open**,
or on macOS 15 and later allow it under **System Settings > Privacy &
Security**. To remove the plugin, delete the file.

Copied by hand from a download instead, the library may carry the quarantine
flag a browser puts on what it downloads, and depending on the version of
macOS be refused without a word. If the plugin does not appear, clear the flag
and restart DeaDBeeF:

    xattr -d com.apple.quarantine ~/Library/Application\ Support/Deadbeef/Plugins/ddb_rubato.dylib

Using it
--------

Select tracks and open the **Rubato BPM Analyser** submenu of the playlist's context menu:

* **Analyse BPM, key and tuning** analyses them. With the windows, a progress
  window follows the scan - it appears only if the scan is still going after a
  moment, and **Cancel** abandons it - and then the results window shows every
  track, and nothing is written until **Update N files** is clicked. Without
  the windows, or with **Write tags without showing the results window** on,
  the tags are written as each track finishes.
* **Double BPM** and **Halve BPM** correct the metrical level of a BPM that is
  already in the file, scaling `INITIALBPM` with it and removing
  `BpmAlgorithm`, because the analysis no longer stands behind the number.
* **Tap BPM of the playing track...**, with the windows, opens the tapping
  window.
* **Analyse without writing tags**, without the windows, analyses and writes
  nothing, and reports what each file's BPM field held beside the new figure.
  With the windows the results window does this, and the entry is not shown.

The analyses are also in the context menu of a playlist's tab, where they
apply to the whole playlist.

Every analysed track also gets a line in the log window, **View > Log**
(**Window > Log Window** on the Mac):

    rubato: [3/12] La cumparsita.flac: 118.52 BPM, Tango (p=0.99), opens at 121.3, +/-2.1; key Dm (high), tuning -19.8 c, retune +0.00% to A=435

A track that cannot be read or written opens the log window by itself; one
that is only skipped does not. **Log the details of each analysis** on the
settings page adds what foo_rubato prints under *Diagnostics*: the beat, the
grid, the key's margin and the timings.

Tracks are scanned several at a time, two short of the machine's logical
processor count, at below-normal priority so that playback is not starved - the
same arrangement as foo_rubato, for the same reasons. Quitting DeaDBeeF during
a scan abandons whatever is left; a track whose analysis was interrupted is
not written.

### The results window

foo_rubato's, column for column: Title, BPM, BPM from tag (only when some
track arrived with one), Initial BPM, Fluctuation, Rhythm, and Key and Tuning
(only when a key was measured). Resting the pointer on a row explains its
tuning and the retune suggestions, and repeats the title where the column cuts
it short. **Double BPM** and **Halve BPM** act on the highlighted rows and
take the attribution off them; **Update N files** writes every row, which is
why it says how many. **Cancel** or Escape writes nothing. Unlike foo_rubato's
it can be resized, and several can be open at once.

### The tapping window

Tap along with whatever is playing - click **Tap**, or press the space bar
anywhere in the window - and the average of the last taps is shown, 30 by
default; a pause of 5 seconds starts again. Both are on the settings page. A
tap lands when the button goes down, not when it comes back up, which would
add however long it was held to every interval.

**Write to playing track** writes to whatever is playing at the moment it is
clicked, which the window names. A tapped BPM replaces the file's BPM and
removes `INITIALBPM` and `BpmAlgorithm`; the key and the tuning, which were
measured, stay.

Settings
--------

**Preferences > Plugins > Rubato BPM Analyzer > Configure** has the choices
foo_rubato's preferences page has - BPM precision, the BPM field name,
`INITIALBPM`, the `BpmAlgorithm`/`KeyAlgorithm` attributions, whether to
measure tuning and key at all and which of `KEY`, `TUNING` and `RETUNE` to
write, the diagnostic detail in the log, and with the windows, writing without
the results window and the two tapping settings - with the same defaults. In
place of foo_rubato's question about overwriting tracks that already have a
BPM, there is **Skip tracks that already have a BPM**, off by default.

Windows
-------

The windows are split in two so that each toolkit's version decides nothing
the others have to decide again:

* `rubato_results.h` / `.cpp` is everything a window shows or does, with no
  toolkit in it: which columns there are, what every cell and tooltip says,
  what doubling a row does, what the commit button is labelled and writes, how
  far a scan has got, and what tapping along measures.
* `rubato_ui.h` is the calls the plugin makes to put a window up -
  `connect`, `shutdown`, `available`, `show_progress`, `show_results`,
  `show_tap` - each callable from any thread. `gtk/ui_gtk.cpp` implements
  them in GTK 3, `cocoa/ui_cocoa.mm` in Cocoa, `ui_none.cpp` as nothing, and
  `CMakeLists.txt` picks one.

The Cocoa windows are Objective-C++ with ARC, laid out in code with Auto
Layout - there are no nibs to ship - and hand work to the main thread with
`dispatch_async` where the GTK ones use `g_idle_add`. They appear when
DeaDBeeF's Cocoa interface, `cocoaui`, is the one running, which in DeaDBeeF
for Mac it always is. They are the GTK windows' layout in Cocoa's idiom:
Escape cancels, Return presses the commit button, and holding the space bar in
the tapping window taps once rather than repeating.

The GTK windows appear only under DeaDBeeF's GTK 3 interface. Under the GTK 2
interface the plugin behaves as a build without windows; that interface is
DeaDBeeF's older one and nothing here is drawn for it. Loading a library
linked against GTK 3 into a GTK 2 process is itself a risk - the two cannot
share a process - so a DeaDBeeF running the GTK 2 interface wants a build with
`-DRUBATO_DDB_GTK=OFF`.

The fields
----------

The fields and the rules are foo_rubato's; the [top-level
README](../README.md#tags) explains each one. `rubato_format.h` is a port of
the header-only parts of foo_rubato that decide them, on `std::string`
instead of pfc, and has to be kept in step with `foo_rubato/bpm_key_format.h`
and `foo_rubato/bpm_tag_fields.h`.

Where DeaDBeeF differs is in what a field name reaches:

| | mp3 (ID3v2) | m4a | FLAC, Ogg, APE |
|---|---|---|---|
| BPM | `BEATS_PER_MINUTE`, which is DeaDBeeF's name for `TBPM` | `BPM`, which DeaDBeeF maps onto `tmpo`; always a whole number | `BPM` |
| the key's standard slot | `INITIAL_KEY`, DeaDBeeF's name for `TKEY` | `initialkey` | `INITIALKEY` |

A BPM field name other than `BPM` on the settings page is written as given, in
every container. In an mp3 a `TXXX:BPM` left by another tagger is removed when
the BPM goes to `TBPM`, so the file does not carry two answers.

Two things DeaDBeeF does that this plugin cannot change:

* **m4a `tmpo` is written as text.** DeaDBeeF reads an iTunes-style integer
  `tmpo` correctly, but whenever it saves an m4a's tags - after this plugin,
  or after any edit in the track properties dialog - it writes `tmpo` back as
  a text atom, which iTunes, TagLib-based players and most DJ software do not
  read as a number. foo_rubato writes the integer atom itself; there is no
  plugin API here to do the same.
* **Tracks inside a cue sheet or other multi-track file cannot be tagged.**
  DeaDBeeF does not write tags into a track that is one part of a file - its
  own track properties dialog and ReplayGain scanner skip them too - so they
  are skipped with a line in the log. They can still be analysed without writing.

Numbers are formatted and parsed without the C library's locale: DeaDBeeF's
GTK interface sets the locale from the environment, and on a desktop with a
decimal comma `printf` would otherwise write `118,52` into the file.

Tests
-----

* `rubato_format_test` checks every string that reaches a file, under a
  decimal-comma locale where the machine has one.
* `ddb_rubato_hosttest` loads the plugin into a stand-in for DeaDBeeF
  (`tests/fake_host.h`) - a playlist, track metadata and a decoder that
  synthesises click tracks, one as float and one as 16-bit audio - and runs
  every action through the worker threads: the fields written per container,
  the attributions, the cue-sheet and unselected tracks left alone, scaling,
  skipping tagged tracks, and every reference given back. It runs once without
  windows and once with a window system that records what it is asked to show
  (`tests/ui_recorder.cpp`), which covers the results window's model end to end
  - columns, cells, doubling a row, Update writing each file once - along with
  a cancelled scan, writing without the window, and tapping. It needs no
  DeaDBeeF and no display, and cannot check DeaDBeeF's own half - what each
  decoder's tag writer does with a field name - which is what the table above
  is from.
* `rubato_gtk_preview`, in a GTK build, puts the real GTK windows up over the
  same stand-in with eight made-up tracks behind them, to look at them without
  the player. Given a directory, it saves each window to a PNG there and
  exits. It is not run by `ctest`, since it needs a display.
  `rubato_cocoa_preview`, in a Cocoa build, does the same for the Cocoa
  windows, over the same tracks (`tests/preview_tracks.h`).

Files
-----

* `rubato_plugin.cpp` - the plugin: actions, settings, the worker threads,
  decoding through DeaDBeeF's decoders, and writing.
* `rubato_format.h` - the strings and field names, with no DeaDBeeF in it.
* `rubato_results.h`, `rubato_results.cpp` - what the windows show and do,
  with no toolkit in it.
* `rubato_ui.h`, `ui_none.cpp`, `gtk/ui_gtk.cpp`, `cocoa/ui_cocoa.mm` - the
  windows.
* `include/deadbeef/deadbeef.h`, `include/deadbeef/gtkui_api.h` - DeaDBeeF's
  plugin header and its GTK interface's, from the 1.10.3 release tag,
  unmodified (zlib licence, in each file).
* `compat/msvc/` - the two POSIX headers `deadbeef.h` wants, for MSVC.
