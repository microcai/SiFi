
#pragma once

#include <boost/math/constants/constants.hpp>
#include "dqueue.hpp"

#define CHIPSIZE 480
#define GAPSIZE 96
#define SAMPLES_PER_CHIP 10

template <class T>
T circumference(T r)
{
    return boost::math::constants::two_pi<T>() * r;
}

extern const int samplerate;
extern std::array<int,3> freq_selector(int freq_channel = 0);
extern dqueue<5, std::vector<uint8_t>> packet_send_buffer;
