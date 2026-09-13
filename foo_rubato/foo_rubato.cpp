/******************************************************************************
*          DO WHAT THE FUCK YOU WANT TO PUBLIC LICENSE
*                    Version 2, December 2004
*
* Copyright (C) 2009-2010, Michael Balzer         Email: fraganator@hotmail.com
* Everyone is permitted to copy and distribute verbatim or modified
* copies of this license document, and changing it is allowed as long
* as the name is changed.
*
*            DO WHAT THE FUCK YOU WANT TO PUBLIC LICENSE
*   TERMS AND CONDITIONS FOR COPYING, DISTRIBUTION AND MODIFICATION
*
*  0. You just DO WHAT THE FUCK YOU WANT TO.
*
* This program is free software. It comes without any warranty, to
* the extent permitted by applicable law. You can redistribute it
* and/or modify it under the terms of the Do What The Fuck You Want
* To Public License, Version 2, as published by Sam Hocevar. See
* http://sam.zoy.org/wtfpl/COPYING for more details.
*
* If the code is redistributed or modified, please drop me an email letting me
* know how the code is being used. It's nice to know where it ends up :)
*
*******************************************************************************
*
* foo_rubato.cpp - A foobar2000 component for automatically detecting a
* song's BPM
*
* REFERENCES:
* BPM estimation algorithm based on:
* Tempo and Beat Estimation of Musical Signals - http://ismir2004.ismir.net/proceedings/p032-page-158-paper191.pdf
*
* OTHER REFERENCES:
* [1] Onset Detection Revisited - http://www.dafx.ca/proceedings/papers/p_133.pdf
* [2] A Comparison of Sound Onset Detection Algorithms with Emphasis on Psychoacoustically Motivated Detection Functions - http://www.cogs.susx.ac.uk/users/nc81/research/comparison.pdf
* [3] Window Functions - http://en.wikipedia.org/wiki/Window_function#Window_examples
*
* REVISION HISTORY:
* Date       | Version      | Description
* -----------------------------------------------------------------------------
* 21/12/2009 | 0.1.0        | Initial release
* -----------------------------------------------------------------------------
* 24/12/2009 | 0.1.1        | Refactored code (with processing time decrease)
*            |              | Initial source code release
* -----------------------------------------------------------------------------
* 31/12/2009 | 0.2.0        | Added confirmation to rescan already tagged files
*            |              | Added ReplayGain style results dialog
*            |              | Added preferences page (with destination BPM tag)
*            |              | Added manual bpm calculation window
* -----------------------------------------------------------------------------
* 02/01/2010 | 0.2.1        | Miscellaneous bug fixes
* -----------------------------------------------------------------------------
* 07/01/2010 | 0.2.2        | Crash report fix
*            |              | Candidate bpm selection can be mode, mean, median
*            |              | added to preferences
*            |              | Debug output added to preferences
* -----------------------------------------------------------------------------
* 11/01/2010 | 0.2.3        | Updated to foobar2000 1.0 SDK
*            |              | Added double/halve buttons to results dialog
*            |              | Added option to auto write tags after analysis
*            |              | Limit preference range inputs
*            |              | Crash report fix (using info not yet cached)
* -----------------------------------------------------------------------------
* 12/01/2010 | 0.2.4        | Crash report fix (component about message)
* -----------------------------------------------------------------------------
* 19/04/2010 | 0.2.4.1      | Numerous bug fixes
* -----------------------------------------------------------------------------
* 2014-02-11 | 0.2.4.2      | Fixed abort checks in worker thread
*            |              | Added basic exception handling in worker thread
*            |              | Numerous refactorings
* -----------------------------------------------------------------------------
* 2014-02-12 | 0.2.4.3      | Preferences page fresh up
*            |              | Enabled dialog navigation in result and manual
*            |              | dialogs
*            |              | Introduced wrapper for FFTW to prepare
*            |              | replacement
* -----------------------------------------------------------------------------
* 2014-02-19 | 0.2.4.4      | Replaced FFTW with KISS FFT
* -----------------------------------------------------------------------------
* 2014-06-04 | 0.2.4.5      | Windows 7 taskbar indicates total track progress
*            |              | Fixed automatic tag writing
* -----------------------------------------------------------------------------
* 2014-06-24 | 0.2.4.6      | Show tag progress window delayed
*            |              | Refactored tag writing for doubling and halving
*            |              | BPM tag
******************************************************************************/

#include "stdafx.h"

#include <SDK/foobar2000.h>

#include "version.h"

// Which FFT licence belongs in the about text below - one, not both.
#include "fft_license.h"

DECLARE_COMPONENT_VERSION(
	FOO_RUBATO_NAME,
	FOO_RUBATO_VERSION,
	"Detects the tempo of a track, and which of Tango, Vals, Milonga or Reggae\n"
	"it is - or none of the four - from the audio alone, without reading the\n"
	"genre tag.\n"
	"\n"
	"The two answers are linked. The tempo a dancer taps is not a property of\n"
	"the audio by itself: a tango is tapped on the beat, a vals once per 3/4\n"
	"bar, a milonga once per 2/4 bar, a reggae on the quarter note - under the\n"
	"skank rather than on it. So the rhythm is settled first and the tempo\n"
	"reported on the level that rhythm implies. Measured against 3,692\n"
	"hand-tapped tracks the tempo lands within 2 BPM of the tap 88.7% of the\n"
	"time, about as close as the same person tapping the same track twice;\n"
	"rhythm classification is 93.6% accurate.\n"
	"\n"
	"Also measured, and shown in the results window: the tempo the track opens\n"
	"at, which on a side that eases off when the singer enters is several BPM\n"
	"above the figure for the whole of it, and how far the tempo moves over the\n"
	"track. Written to tags: the BPM, the opening tempo as INITIALBPM, and\n"
	"BpmAlgorithm recording which analysis produced them.\n"
	"\n"
	"Built against the foobar2000 SDK " FOO_RUBATO_SDK_VERSION "; runs on 32 and\n"
	"64 bit foobar2000.\n"
	"\n"
	"(c) 2009-2014 Michael Balzer (fraganator@hotmail.com)\n"
	"(c) 2014 Holger Stenger\n"
	"(c) 2026 Nick Shaforostov\n"
	"\n"
	"How the tempo engine and the rhythm classifier were derived and measured\n"
	"is written up in docs/tango-analysis.md in the source tree.\n"
	"\n"
	"References:\n"
	"- Tempo and Beat Estimation of Musical Signals\n"
	"  http://ismir2004.ismir.net/proceedings/p032-page-158-paper191.pdf\n"
	"\n"
	"- Onset Detection Revisited\n"
	"  http://www.dafx.ca/proceedings/papers/p_133.pdf\n"
	"\n"
	"- A Comparison of Sound Onset Detection Algorithms with Emphasis on Psychoacoustically Motivated Detection Functions\n"
	"  http://www.cogs.susx.ac.uk/users/nc81/research/comparison.pdf\n"
	"\n"
	"- Window Functions\n"
	"  http://en.wikipedia.org/wiki/Window_function#Window_examples\n"
	"\n"
	"Used libraries:\n"
	"\n"
	FOO_RUBATO_FFT_LICENSE
);

VALIDATE_COMPONENT_FILENAME("foo_rubato.dll");
