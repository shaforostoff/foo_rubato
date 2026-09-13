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
	"KISS FFT (http://sourceforge.net/projects/kissfft/)\n"
	"==== start of KISS FFT license ====\n"
	"Copyright (c) 2003-2010 Mark Borgerding\n"
	"\n"
	"All rights reserved.\n"
	"\n"
	"Redistribution and use in source and binary forms, with or without modification, are permitted provided that the following conditions are met:\n"
	"\n"
    "* Redistributions of source code must retain the above copyright notice, this list of conditions and the following disclaimer.\n"
    "* Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the following disclaimer in the documentation and/or other materials provided with the distribution.\n"
    "* Neither the author nor the names of any contributors may be used to endorse or promote products derived from this software without specific prior written permission.\n"
	"\n"
	"THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS \"AS IS\" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.\n"
	"==== end of KISS FFT license ====\n"
	"\n"
	"PFFFT (https://bitbucket.org/jpommier/pffft)\n"
	"==== start of PFFFT license ====\n"
	"Copyright (c) 2013  Julien Pommier ( pommier@modartt.com )\n"
	"\n"
	"Based on original fortran 77 code from FFTPACKv4 from NETLIB,\n"
	"authored by Dr Paul Swarztrauber of NCAR, in 1985.\n"
	"\n"
	"As confirmed by the NCAR fftpack software curators, the following\n"
	"FFTPACKv5 license applies to FFTPACKv4 sources. My changes are\n"
	"released under the same terms.\n"
	"\n"
	"FFTPACK license:\n"
	"\n"
	"http://www.cisl.ucar.edu/css/software/fftpack5/ftpk.html\n"
	"\n"
	"Copyright (c) 2004 the University Corporation for Atmospheric\n"
	"Research (\"UCAR\"). All rights reserved. Developed by NCAR's\n"
	"Computational and Information Systems Laboratory, UCAR,\n"
	"www.cisl.ucar.edu.\n"
	"\n"
	"Redistribution and use of the Software in source and binary forms,\n"
	"with or without modification, is permitted provided that the\n"
	"following conditions are met:\n"
	"\n"
	"- Neither the names of NCAR's Computational and Information Systems\n"
	"Laboratory, the University Corporation for Atmospheric Research,\n"
	"nor the names of its sponsors or contributors may be used to\n"
	"endorse or promote products derived from this Software without\n"
	"specific prior written permission.\n"
	"\n"
	"- Redistributions of source code must retain the above copyright\n"
	"notices, this list of conditions, and the disclaimer below.\n"
	"\n"
	"- Redistributions in binary form must reproduce the above copyright\n"
	"notice, this list of conditions, and the disclaimer below in the\n"
	"documentation and/or other materials provided with the\n"
	"distribution.\n"
	"\n"
	"THIS SOFTWARE IS PROVIDED \"AS IS\", WITHOUT WARRANTY OF ANY KIND,\n"
	"EXPRESS OR IMPLIED, INCLUDING, BUT NOT LIMITED TO THE WARRANTIES OF\n"
	"MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND\n"
	"NONINFRINGEMENT. IN NO EVENT SHALL THE CONTRIBUTORS OR COPYRIGHT\n"
	"HOLDERS BE LIABLE FOR ANY CLAIM, INDIRECT, INCIDENTAL, SPECIAL,\n"
	"EXEMPLARY, OR CONSEQUENTIAL DAMAGES OR OTHER LIABILITY, WHETHER IN AN\n"
	"ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN\n"
	"CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS WITH THE\n"
	"SOFTWARE.\n"
	"==== end of PFFFT license ===="
);

VALIDATE_COMPONENT_FILENAME("foo_rubato.dll");
