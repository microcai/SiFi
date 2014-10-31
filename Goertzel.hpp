
#include <array>
#include "common.hpp"
/*
 * Goertzel 算法检测频点
 */
template<unsigned windowssize>
static double Goertzel_frequency_detector(const std::array<float, windowssize> & test_window,
	double target_frequency, unsigned samplerate)
{
	double normalized_frequency = target_frequency / samplerate;

	auto coeff = 2 * std::cos(circumference(normalized_frequency));

	auto s_prev = 0.0 , s_prev2 = 0.0;

	for( auto x : test_window )
	{
		auto s = x + coeff* s_prev - s_prev2;
		s_prev2 = s_prev;
		s_prev = s;
	}

	auto power = s_prev2*s_prev2 + s_prev*s_prev - coeff*s_prev*s_prev2;
	return std::log10(power) - 2.5;
}
