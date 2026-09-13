#ifndef FOO_RUBATO_FFT_LICENSE_H
#define FOO_RUBATO_FFT_LICENSE_H

// The licence of the transform this DLL was actually built with, and only that
// one.
//
// bpmcore links one FFT and not both - cmake/fft_backend.cmake decides which -
// so the about box credits one. Carrying both would put the notice of a library
// that is not in the binary in front of the reader, which is the opposite of
// what a notice is for, and would throw away the one thing the list does say
// about an installed component: which transform produced its numbers.
//
// A macro rather than a string constant because DECLARE_COMPONENT_VERSION takes
// a compile-time constant and the about text is assembled by string literal
// concatenation. A #if cannot live inside a macro argument list, so the choice
// is made out here at file scope and foo_rubato.cpp names the result.

#if defined(BPMCORE_FFT_PFFFT)

#define FOO_RUBATO_FFT_LICENSE \
	"PFFFT (https://bitbucket.org/jpommier/pffft)\n" \
	"==== start of PFFFT license ====\n" \
	"Copyright (c) 2013  Julien Pommier ( pommier@modartt.com )\n" \
	"\n" \
	"Based on original fortran 77 code from FFTPACKv4 from NETLIB,\n" \
	"authored by Dr Paul Swarztrauber of NCAR, in 1985.\n" \
	"\n" \
	"As confirmed by the NCAR fftpack software curators, the following\n" \
	"FFTPACKv5 license applies to FFTPACKv4 sources. My changes are\n" \
	"released under the same terms.\n" \
	"\n" \
	"FFTPACK license:\n" \
	"\n" \
	"http://www.cisl.ucar.edu/css/software/fftpack5/ftpk.html\n" \
	"\n" \
	"Copyright (c) 2004 the University Corporation for Atmospheric\n" \
	"Research (\"UCAR\"). All rights reserved. Developed by NCAR's\n" \
	"Computational and Information Systems Laboratory, UCAR,\n" \
	"www.cisl.ucar.edu.\n" \
	"\n" \
	"Redistribution and use of the Software in source and binary forms,\n" \
	"with or without modification, is permitted provided that the\n" \
	"following conditions are met:\n" \
	"\n" \
	"- Neither the names of NCAR's Computational and Information Systems\n" \
	"Laboratory, the University Corporation for Atmospheric Research,\n" \
	"nor the names of its sponsors or contributors may be used to\n" \
	"endorse or promote products derived from this Software without\n" \
	"specific prior written permission.\n" \
	"\n" \
	"- Redistributions of source code must retain the above copyright\n" \
	"notices, this list of conditions, and the disclaimer below.\n" \
	"\n" \
	"- Redistributions in binary form must reproduce the above copyright\n" \
	"notice, this list of conditions, and the disclaimer below in the\n" \
	"documentation and/or other materials provided with the\n" \
	"distribution.\n" \
	"\n" \
	"THIS SOFTWARE IS PROVIDED \"AS IS\", WITHOUT WARRANTY OF ANY KIND,\n" \
	"EXPRESS OR IMPLIED, INCLUDING, BUT NOT LIMITED TO THE WARRANTIES OF\n" \
	"MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND\n" \
	"NONINFRINGEMENT. IN NO EVENT SHALL THE CONTRIBUTORS OR COPYRIGHT\n" \
	"HOLDERS BE LIABLE FOR ANY CLAIM, INDIRECT, INCIDENTAL, SPECIAL,\n" \
	"EXEMPLARY, OR CONSEQUENTIAL DAMAGES OR OTHER LIABILITY, WHETHER IN AN\n" \
	"ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN\n" \
	"CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS WITH THE\n" \
	"SOFTWARE.\n" \
	"==== end of PFFFT license ===="

#elif defined(BPMCORE_FFT_KISS)

#define FOO_RUBATO_FFT_LICENSE \
	"KISS FFT (http://sourceforge.net/projects/kissfft/)\n" \
	"==== start of KISS FFT license ====\n" \
	"Copyright (c) 2003-2010 Mark Borgerding\n" \
	"\n" \
	"All rights reserved.\n" \
	"\n" \
	"Redistribution and use in source and binary forms, with or without modification, are permitted provided that the following conditions are met:\n" \
	"\n" \
    "* Redistributions of source code must retain the above copyright notice, this list of conditions and the following disclaimer.\n" \
    "* Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the following disclaimer in the documentation and/or other materials provided with the distribution.\n" \
    "* Neither the author nor the names of any contributors may be used to endorse or promote products derived from this software without specific prior written permission.\n" \
	"\n" \
	"THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS \"AS IS\" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.\n" \
	"==== end of KISS FFT license ===="

#else

// Neither define arrived, so bpmcore's settings did not reach this target.
// There is nothing safe to assume here: whichever licence this fell back on
// would be a coin toss printed in a shipping about box, and it would say
// nothing at build time. See bpmcore/CMakeLists.txt.
#error "No FFT backend define from bpmcore; foo_rubato cannot tell which licence it ships."

#endif

#endif // FOO_RUBATO_FFT_LICENSE_H
