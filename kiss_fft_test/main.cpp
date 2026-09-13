// Regression test for kiss_fft itself, against stored reference spectra.
//
// It checks a real input transformed and then packed half-complex as
//
//     out[0..n/2]   real parts
//     out[n-k]      imaginary part of bin k, for k in 1..(n-1)/2
//
// Each fft*.txt file holds one case: a size, the real input, and the expected
// packed output. Exit status is 0 only if every case is within tolerance, so
// this is usable as a CTest test.
//
// The harness previously carried a hand-written std::allocator subclass that
// sized allocations in bytes rather than elements, could not satisfy modern
// std::vector, and was pointless anyway since KISS_FFT_MALLOC is malloc here.
// It also returned 0 whatever happened and waited for a keypress.

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

#include <kiss_fft/kiss_fft.h>

namespace {

struct fft_case
{
	std::size_t size = 0;
	std::vector<double> input;
	std::vector<double> expected_output;
	std::vector<double> actual_output;

	void create(std::size_t n)
	{
		size = n;
		input.assign(n, 0.0);
		expected_output.assign(n, 0.0);
		actual_output.assign(n, 0.0);
	}

	void transform()
	{
		kiss_fft_cfg cfg = kiss_fft_alloc(static_cast<int>(size), 0, nullptr, nullptr);
		if (cfg == nullptr) throw std::runtime_error("kiss_fft_alloc failed");

		std::vector<kiss_fft_cpx> cpx_input(size);
		std::vector<kiss_fft_cpx> cpx_output(size);

		for (std::size_t k = 0; k < size; ++k)
		{
			cpx_input[k].r = static_cast<kiss_fft_scalar>(input[k]);
			cpx_input[k].i = static_cast<kiss_fft_scalar>(0);
		}

		kiss_fft(cfg, cpx_input.data(), cpx_output.data());
		kiss_fft_free(cfg);

		for (std::size_t k = 0; k <= size / 2; ++k)
			actual_output[k] = cpx_output[k].r;

		for (std::size_t k = 1; k < (size + 1) / 2; ++k)
			actual_output[size - k] = cpx_output[k].i;
	}

	double distance() const
	{
		double d = 0.0;
		for (std::size_t k = 0; k < size; ++k)
			d = std::max(d, std::abs(expected_output[k] - actual_output[k]));
		return d;
	}
};

void expect_keyword(std::istream & is, const char * expected)
{
	std::string keyword;
	is >> keyword;
	if (keyword != expected)
		throw std::runtime_error(std::string("expected '") + expected + "' keyword, got '" + keyword + "'");
}

std::istream & operator >>(std::istream & is, fft_case & test)
{
	expect_keyword(is, "size");
	std::size_t size = 0;
	is >> size;
	if (!is || size == 0) throw std::runtime_error("bad size");
	test.create(size);

	expect_keyword(is, "input");
	for (std::size_t i = 0; i < size; ++i) is >> test.input[i];

	expect_keyword(is, "output");
	for (std::size_t i = 0; i < size; ++i) is >> test.expected_output[i];

	if (!is) throw std::runtime_error("truncated test case");
	return is;
}

//! Absolute error a case is allowed, at the width kiss was built for.
//!
//! Two floors, and the larger wins. The stored expectations carry nine
//! decimals, so a little absolute slack is needed at any width: 1e-6 is what
//! this test has always used, and the double build measures three orders
//! inside it. Float cannot meet that floor and should not be asked to - the
//! bins here reach a magnitude of 32, where a single float epsilon is already
//! 4e-6 - so above it sits a term relative to the largest bin, which is what
//! an FFT's error is actually proportional to. Eight epsilons of headroom
//! leaves the measured float error a factor of thirty clear while staying far
//! below anything a broken transform could produce.
double tolerance(const fft_case & test)
{
	double peak = 0.0;
	for (std::size_t k = 0; k < test.size; ++k)
		peak = std::max(peak, std::abs(test.expected_output[k]));
	return std::max(1e-6, peak * 8 * std::numeric_limits<kiss_fft_scalar>::epsilon());
}

bool run_case(const std::string & path)
{
	fft_case test;

	std::ifstream is(path.c_str());
	if (!is) throw std::runtime_error("cannot open " + path);
	is >> test;

	test.transform();

	const double threshold = tolerance(test);
	const double d = test.distance();
	const bool ok = d < threshold;

	std::cout << (ok ? "PASS  " : "FAIL  ") << path
	          << "  size " << test.size
	          << "  max error " << std::scientific << std::setprecision(3) << d << "\n";

	if (!ok)
	{
		std::cout << "      differing bins:";
		for (std::size_t k = 0; k < test.size; ++k)
		{
			if (!(std::abs(test.expected_output[k] - test.actual_output[k]) < threshold))
				std::cout << " " << k;
		}
		std::cout << "\n";
	}

	return ok;
}

} // namespace

int main(int argc, char ** argv)
{
	// The data files live next to the sources, so CTest passes their directory.
	std::string dir = (argc > 1) ? argv[1] : ".";
	if (!dir.empty() && dir.back() != '/' && dir.back() != '\\') dir += '/';

	static const char * const cases[] = { "fft16.txt", "fft32.txt", "fft64.txt" };

	bool all_ok = true;
	try
	{
		for (const char * name : cases)
			all_ok &= run_case(dir + name);
	}
	catch (const std::exception & e)
	{
		std::cerr << "error: " << e.what() << "\n";
		return 2;
	}

	std::cout << (all_ok ? "all cases passed\n" : "FAILURES\n");
	return all_ok ? 0 : 1;
}
