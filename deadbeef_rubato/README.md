Rubato BPM Analyzer for DeaDBeeF
================================

The same analysis as the foobar2000 component - `bpmcore`, unchanged - as a
[DeaDBeeF](https://deadbeef.sourceforge.io/) plugin. It measures the tempo,
the rhythm (Tango, Vals, Milonga, Reggae or none of them), the tuning and the
key of the selected tracks, and writes them to the files under the same field
names and the same rules as foo_rubato, so a library tagged by either player
reads the same in the other.

It needs DeaDBeeF 1.8.0 or later (plugin API 1.10), and builds for Windows,
macOS and Linux from one source file.

Building
--------

Only a C++17 compiler and CMake 3.16 or later. Nothing is downloaded: the
DeaDBeeF plugin header is in `include/`, and `bpmcore`, PFFFT and KISS FFT are
built from the directories beside this one. Run these from the repository
root.

### Windows, Visual Studio

    cmake -S deadbeef_rubato -B build\ddb-x64 -A x64
    cmake --build build\ddb-x64 --config Release
    ctest --test-dir build\ddb-x64 -C Release

writes `build\ddb-x64\Release\ddb_rubato.dll`. The C runtime is linked
statically, so it needs no Visual C++ redistributable. DeaDBeeF itself is
built with MinGW, which does not matter: the plugin interface is a table of C
function pointers and nothing allocated on one side is freed on the other.
`deadbeef.h` includes two POSIX headers MSVC does not have; `compat/msvc/`
stands in for them and is on the include path for MSVC builds only.

Build `-A x64` for the 64-bit DeaDBeeF, which is the one its site offers; a
plugin loads only into a player of its own architecture.

### Windows, MSYS2 MinGW

From a UCRT64 or MINGW64 shell with `mingw-w64-ucrt-x86_64-toolchain` (or the
`x86_64` equivalent), `cmake` and `ninja` installed:

    cmake -S deadbeef_rubato -B build/ddb-mingw -G Ninja -DCMAKE_BUILD_TYPE=Release
    cmake --build build/ddb-mingw
    ctest --test-dir build/ddb-mingw

libstdc++, libgcc and winpthreads are linked in statically, so the one DLL is
all there is to copy.

### macOS

    cmake -S deadbeef_rubato -B build/ddb-mac -DCMAKE_BUILD_TYPE=Release
    cmake --build build/ddb-mac
    ctest --test-dir build/ddb-mac

writes `build/ddb-mac/ddb_rubato.dylib`, universal (Apple Silicon and Intel)
and ad-hoc signed. The signature is not optional - Apple Silicon will not load
unsigned code - but it need not be a real one, because DeaDBeeF does not run
under the hardened runtime. `-DRUBATO_CODESIGN_IDENTITY="Developer ID
Application: ..."` signs with a real identity instead, and
`-DCMAKE_OSX_ARCHITECTURES=arm64` builds for one architecture only.

### Linux

    cmake -S deadbeef_rubato -B build/ddb -DCMAKE_BUILD_TYPE=Release
    cmake --build build/ddb
    ctest --test-dir build/ddb

writes `build/ddb/ddb_rubato.so`.

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

Using it
--------

Select tracks and open the **Rubato** submenu of the playlist's context menu:

* **Analyse BPM, key and tuning** analyses them and writes the tags.
* **Analyse without writing tags** analyses them and writes nothing, and
  reports what each file's BPM field held beside the new figure.
* **Double BPM** and **Halve BPM** correct the metrical level of a BPM that is
  already there, scaling `INITIALBPM` with it and removing `BpmAlgorithm`,
  because the analysis no longer stands behind the number.

The analyses are also in the context menu of a playlist's tab, where they
apply to the whole playlist.

Results go to the log window, **View > Log**, one line per track:

    rubato: [3/12] La cumparsita.flac: 118.52 BPM, Tango (p=0.99), opens at 121.3, +/-2.1; key Dm (high), tuning -19.8 c, retune +0.00% to A=435

There is no results window and no tapping window. DeaDBeeF has no
toolkit-neutral way to put up a window - the GTK and Cocoa interfaces are each
a plugin of their own - so anything here with a window would be two plugins,
one per toolkit. What the foobar2000 results window is for is covered by the
log line and by analysing without writing first. A track that cannot be read
or written opens the log window by itself; one that is only skipped does not.
**Log the details of each analysis** on the settings page adds what
foo_rubato prints under *Diagnostics*: the beat, the grid, the key's margin
and the timings.

Tracks are scanned several at a time, two short of the machine's logical
processor count, at below-normal priority so that playback is not starved - the
same arrangement as foo_rubato, for the same reasons. Quitting DeaDBeeF during
a scan abandons whatever is left; a track whose analysis was interrupted is
not written.

Settings
--------

**Preferences > Plugins > Rubato BPM Analyzer > Configure** has the choices
foo_rubato's preferences page has - BPM precision, the BPM field name,
`INITIALBPM`, the `BpmAlgorithm`/`KeyAlgorithm` attributions, whether to
measure tuning and key at all and which of `KEY`, `TUNING` and `RETUNE` to
write, and the diagnostic detail in the log - with the same defaults. In place
of foo_rubato's question about overwriting tracks that already have a BPM,
there is **Skip tracks that already have a BPM**, off by default.

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
* `ddb_rubato_hosttest` loads the plugin into a stand-in for DeaDBeeF - a
  playlist, track metadata and a decoder that synthesises click tracks, one
  as float and one as 16-bit audio - and runs all four actions through the
  worker threads: the fields written per container, the attributions, the
  cue-sheet and unselected tracks left alone, scaling, skipping tagged tracks,
  and every reference given back. It needs no DeaDBeeF installed, and cannot
  check DeaDBeeF's own half - what each decoder's tag writer does with a field
  name - which is what the table above is from.

Files
-----

* `rubato_plugin.cpp` - the plugin: actions, settings, the worker threads,
  decoding through DeaDBeeF's decoders, and writing.
* `rubato_format.h` - the strings and field names, with no DeaDBeeF in it.
* `include/deadbeef/deadbeef.h` - DeaDBeeF's plugin header, from the 1.10.3
  release tag, unmodified (zlib licence, in the file).
* `compat/msvc/` - the two POSIX headers `deadbeef.h` wants, for MSVC.
