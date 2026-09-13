// Checks pffft against kiss_fft, at the sizes bpmcore transforms.
//
// This is the test that makes it safe to swap the transform. The collection
// sweep settled the width - float costs nothing, measured over 12,157 tracks -
// but a different library is a different question: pffft reorders its output,
// packs the two real-valued bins together, and computes with different
// butterflies in a different order. None of that can be argued from the source;
// it has to be run.
//
// So both are built into one binary at the same width, handed the same frames,
// and required to agree. kiss is the reference: it is plain C that compiles the
// same everywhere, which is exactly what a reference has to be. Run this on any
// new architecture before trusting the fast path on it.
//
// It also prints pffft_simd_size(), because pffft falls back to scalar code
// silently when it cannot find SIMD, and a fallback that says nothing is how a
// build ends up six times slower than it was supposed to be.

#include <pffft/pffft.h>
#include <kiss_fft/kiss_fftr.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <vector>

namespace
{
	//! Sizes bpmcore asks for. 512 to 4096 are the model rate times a power of
	//! two - 11.025, 22.05, 44.1 and 88.2kHz - and are what every track that
	//! takes the ordinary route is transformed at. 96 and 160 are not reachable
	//! that way; they are here because they are the smallest sizes that are
	//! 5-smooth multiples of 32 without being powers of two, which is the case
	//! the odd-rate fallback can land on and the one a radix-2-only assumption
	//! would break.
	const int sizes[] = { 96, 160, 512, 1024, 1920, 2048, 4096 };

	//! Worst disagreement over `frames` random frames, relative to the largest
	//! magnitude in the spectrum.
	//!
	//! Relative because an FFT's error is proportional to what it is summing,
	//! not absolute; and against the peak rather than per bin because a bin
	//! that is near zero by cancellation has no significant digits to agree on
	//! and would make any two correct implementations look far apart.
	double worst_relative_error(int nfft, int frames, bool & sane)
	{
		const int nbin = nfft / 2 + 1;

		kiss_fftr_cfg kcfg = kiss_fftr_alloc(nfft, 0, nullptr, nullptr);
		PFFFT_Setup * pcfg = pffft_new_setup(nfft, PFFFT_REAL);
		float * pin = static_cast<float *>(pffft_aligned_malloc(sizeof(float) * nfft));
		float * pout = static_cast<float *>(pffft_aligned_malloc(sizeof(float) * (nfft + 2)));
		float * pwork = static_cast<float *>(pffft_aligned_malloc(sizeof(float) * nfft));
		std::vector<kiss_fft_scalar> kin(nfft);
		std::vector<kiss_fft_cpx> kout(nbin);

		sane = kcfg != nullptr && pcfg != nullptr &&
		       pin != nullptr && pout != nullptr && pwork != nullptr;
		double worst = 0.0;

		if (sane)
		{
			std::mt19937 rng(12345 + nfft);
			std::uniform_real_distribution<double> noise(-1.0, 1.0);

			for (int f = 0; f < frames && sane; f++)
			{
				// Tones plus noise, windowed, which is the shape of what the
				// analysis actually transforms - not white noise, whose flat
				// spectrum would hide an error in the low bins where the bands
				// that matter live.
				const double f0 = 3.0 + 40.0 * (f % 7);
				for (int i = 0; i < nfft; i++)
				{
					const double w = 0.5 * (1.0 - std::cos(6.283185307179586 * i / (nfft - 1)));
					const double v = w * (std::sin(6.283185307179586 * f0 * i / nfft)
					                    + 0.4 * std::sin(6.283185307179586 * (f0 * 3.7) * i / nfft)
					                    + 0.15 * noise(rng));
					kin[i] = static_cast<kiss_fft_scalar>(v);
					pin[i] = static_cast<float>(v);
				}

				kiss_fftr(kcfg, kin.data(), kout.data());
				pffft_transform_ordered(pcfg, pin, pout, pwork, PFFFT_FORWARD);

				// The same unpacking real_fft.cpp does: pffft carries F(0) and
				// F(nfft/2), both real, together in its first complex slot.
				const double dc = pout[0], nyquist = pout[1];

				double peak = 0.0;
				for (int k = 0; k < nbin; k++)
				{
					const double r = kout[k].r, i = kout[k].i;
					peak = std::max(peak, std::sqrt(r * r + i * i));
				}
				if (!(peak > 0.0)) { sane = false; break; }

				for (int k = 0; k < nbin; k++)
				{
					double pr, pi;
					if (k == 0)             { pr = dc;      pi = 0.0; }
					else if (k == nfft / 2) { pr = nyquist; pi = 0.0; }
					else                    { pr = pout[2 * k]; pi = pout[2 * k + 1]; }
					const double dr = pr - kout[k].r, di = pi - kout[k].i;
					worst = std::max(worst, std::sqrt(dr * dr + di * di) / peak);
				}
			}
		}

		if (kcfg != nullptr) kiss_fftr_free(kcfg);
		if (pcfg != nullptr) pffft_destroy_setup(pcfg);
		pffft_aligned_free(pin);
		pffft_aligned_free(pout);
		pffft_aligned_free(pwork);
		return worst;
	}
}

int main()
{
	const int simd = pffft_simd_size();
	std::printf("pffft SIMD width: %d float%s%s\n", simd, simd == 1 ? "" : "s",
	            simd == 1 ? "   *** scalar fallback - pffft found no SIMD ***" : "");
	std::printf("kiss_fft_scalar is %s\n", sizeof(kiss_fft_scalar) == sizeof(float)
	            ? "float" : "double");

	// Both are correct implementations at float, so they agree to a few float
	// epsilons and no closer. 32 of them is loose enough that neither rounding
	// nor a different summation order can trip it, and far tighter than any
	// real defect - a wrong twiddle, a mis-packed bin or a permuted output all
	// land at order 1, seven decades above this.
	const double limit = 32.0 * 1.1920929e-7;
	bool all_ok = true;

	for (int nfft : sizes)
	{
		bool sane = false;
		const double e = worst_relative_error(nfft, 24, sane);
		if (!sane)
		{
			std::printf("FAIL  N=%-5d  could not set both transforms up\n", nfft);
			all_ok = false;
			continue;
		}
		const bool ok = e < limit;
		all_ok &= ok;
		std::printf("%s  N=%-5d  worst relative error %.3e  (limit %.3e)\n",
		            ok ? "PASS" : "FAIL", nfft, e, limit);
	}

	// Note, rather than a check: pffft enforces its size restriction with
	// assert() alone (see pffft_new_setup in pffft.c), so a release build
	// hands back a setup for an unsupported N - 1000, say, which is 5-smooth
	// but not a multiple of 32 - and then transforms it into a wrong answer
	// with no complaint at all. Asking here whether it refuses would be
	// testing a property it does not have.
	//
	// What stands in the way is bpmcore's own guard, `fft_size_for` and
	// `fft_size_supported` in real_fft.h, checked by the `fft_sizes` case in
	// bpmcore_test, which can see which backend shipped. Every size above is
	// one that guard can return.

	std::printf("%s\n", all_ok ? "all sizes agree" : "FAILURES");
	return all_ok ? 0 : 1;
}
