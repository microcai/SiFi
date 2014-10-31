
#include <cmath>
#include <iostream>
#include <algorithm>
#include <bitset>
#include <array>
#include <thread>
#include <functional>
#include <boost/asio/buffer.hpp>
#include <boost/coroutine/asymmetric_coroutine.hpp>

#include "common.hpp"
#include "dqueue.hpp"

const int samplerate = 48000;

static std::array<int,3> target_frequency[] =
{
    { 2000 , 4800 ,0},

    { 300 , 330 ,0},
    { 370 , 400 ,0},

    { 450 , 480 ,0},
    { 550 , 580 ,0},

    { 680 , 710 ,0},
    { 860 , 890 ,0 },
};

// 根据当前频率
std::array<int,3> freq_selector(int freq_channel)
{
    return target_frequency[freq_channel];
};


dqueue<5, std::vector<uint8_t>> packet_send_buffer;


static 	int t=0;

#include "baseband.hpp"

#include "RF_antenna.hpp"

void send_packet(std::vector<uint8_t> packet)
{
    packet_send_buffer.push(packet);
}

void File_Rx();

int main(int argc, char **argv)
{
    std::vector<uint8_t> packet = { 'H', 'e', 'l', 'l', 'o', ' ', 'w', 'o', 'r', 'l', 'd' };


//	std::thread Rxthread(SiFi_Rx);
    std::thread Txthread(SiFi_Tx);

    std::thread TestRxthread(File_Rx);

    //sleep(1);

    send_packet(packet);

    Txthread.join();
    return 0;
}
