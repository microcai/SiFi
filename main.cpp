
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
#include "pulseaudio.hpp"

static const int samplerate = 48000;

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
auto freq_selector = []( int freq_channel = 0)
{
	return target_frequency[freq_channel];
};

static void packet_decoder(boost::coroutines::asymmetric_coroutine<uint8_t>::pull_type & source)
{
	// 一次拿到一个字节
	while(source)
	{
		std::cout << "got one byte: " << source.get() << std::endl;
		source();
	}
}


static dqueue<5, std::vector<uint8_t>> packet_send_buffer;

// 数据打包编码
static void datapacker(boost::coroutines::asymmetric_coroutine<uint8_t>::push_type & sink)
{
	// 数据打包，就将数据按照一定的字节封装起来
	// 每个封装后的大小为 62 字节，占用传输时间 0.5s

	if(packet_send_buffer.empty())
		return;

	// 拿到一个包
	auto packet = packet_send_buffer.pull();

	// 按照一定的次序送出
	for(auto b : packet)
		sink(b);
}

static void bytes_laches(boost::coroutines::asymmetric_coroutine<uint8_t>::pull_type & halfbytesource,
	boost::coroutines::asymmetric_coroutine<uint8_t>::push_type & bytestreamsink)
{
	while(halfbytesource && bytestreamsink)
	{
		uint8_t bh = halfbytesource.get();
		halfbytesource();
		if(!halfbytesource) return;
		uint8_t bl = halfbytesource.get();
		halfbytesource();
		bytestreamsink((bh << 4) | bl );
	}
}

static 	int t=0;

/* 信道解码*/
static void channel_decode(boost::coroutines::asymmetric_coroutine<bool>::pull_type & source)
{
	#include "4b5btable.ipp"

	std::bitset<5> codegroup = {0};

	while(source)
	{

		bool bit = source.get();
		std::fprintf(stderr, "\r[%08d] get %d !!!!!!!!!!!\n", t, bit);

		codegroup <<= 1;
		codegroup |= bit;
		source();

		// 00000 序列表示重启解码器
		// 11111 序列表示一个帧开始

		if( codegroup == 0b00000)
		{
			continue;
		}

		// 找到同步序列
		if( codegroup == 0b11111)
		{
			boost::coroutines::asymmetric_coroutine<uint8_t>::push_type pipeline_feed(
				std::bind(
					&bytes_laches,
					std::placeholders::_1,
					boost::coroutines::asymmetric_coroutine<uint8_t>::push_type(packet_decoder)
				)
			);

			while(source)
			{
				// 一次拉 5 个
				for(int i=0; i < 5 ; i++)
				{
					bool bit = source.get();
					codegroup <<= 1;
					codegroup |= bit;
					source();
				}
				// 传递流水线执行数据包解码
				if( codegroup != 0b11111)
					pipeline_feed(FFM_decode_table[codegroup.to_ulong()]);
				else
					break;
			}
		}
	}
}

// 基带信号采样恢复, 每一个采样点为 1/6000s， 通过数数就知道0和1，以及持续了多久
static void BaseBand_Decode(boost::coroutines::asymmetric_coroutine<bool>::pull_type & source)
{
	bool prev_bit = 0;
	bool current_bit = 0;

	boost::coroutines::asymmetric_coroutine<bool>::push_type Decoder(channel_decode);

	while(source)
	{
		bool bit = source.get();

//		std::fprintf(stderr, "[%d]and get %s \n", t, bit?"1":"0");


		int count = 0;
		int abscount = 0;

		while(source && bit == prev_bit)
		{
			source();
			count ++;
			abscount += bit;
			if( count == 10 )
			{
				// 达到 10 个，输出一个 bit 位
				Decoder(current_bit);
				current_bit = 0;
				count = 0;
			}
			bit = source.get();
		}
		// 经历了超长时间的低频率，意思重启解码器
		if( abscount >= 80)
		{
			if( bit )
			{
				// 持续很长时间的高频载波，解码出错
				// 开始进入一直等待 低频信号
				while(source && bit == 0)
				{
					source();
					bit = source.get();
				}
				prev_bit = 0;
				current_bit = 0;
				// 输入5个 0 重启解码器
				Decoder(0);
				Decoder(0);
				Decoder(0);
				Decoder(0);
				Decoder(0);

				source();
				continue;
			}
		}

		if( count > 5)
		{
			Decoder(current_bit);
		}

		current_bit = 1;
		prev_bit = bit;
		source();
	}
}

// 基带信号滤波
template<unsigned samples_per_chip>
static void baseband_filter(boost::coroutines::asymmetric_coroutine<std::tuple<double,double,double>>::pull_type & source)
{
	double max_power = 0.0;

	const double mix_signal_power = -30.0;

	while(source)
	{
		auto signalpair = source.get();
		source();

		auto freq1 = std::get<0>(signalpair);
		auto freq2 = std::get<1>(signalpair);
		auto snr = std::get<2>(signalpair);

		if( max_power <  freq1 + freq2)
		{
			max_power = freq1 + freq2;
		}

		if( snr < mix_signal_power )
		{
			std::fprintf(stderr, "\r[%08d] too low SNR %2.4f db", t, (float)snr);
			continue;
		}

		std::array<uint8_t, samples_per_chip> cap;
		std::fill(std::begin(cap), std::end(cap),0);

		// 预填充
		for(int i = samples_per_chip/2 ; i < samples_per_chip && source && ( snr >= mix_signal_power); i++)
		{
			// 预填充 4 个信号点
			cap[i] = (freq1 < freq2);

			signalpair = source.get(); source();
			freq1 = std::get<0>(signalpair);
			freq2 = std::get<1>(signalpair);
			snr = std::get<2>(signalpair);
		}

		if( freq1 + freq2 < mix_signal_power )
		{
			std::fprintf(stderr, "\r[%08d] no signal !!!!!!!!!!!\n", t);
			continue;
		}

		// 建立一个带通滤波解码器
		boost::coroutines::asymmetric_coroutine<bool>::push_type Decoder(BaseBand_Decode);

		do
		{
			std::move(std::begin(cap)+1, std::end(cap), std::begin(cap));
			cap.back() = (freq1 < freq2);

			// 整理出 samples_per_chip 个信号点并平均化
			Decoder( std::accumulate(std::begin(cap), std::end(cap),0) > (samples_per_chip/2) );

		//	std::fprintf(stderr, "\r[%08d] yes signal (%030f, %030f)", t, freq1 , freq2);

			signalpair = source.get();
			source();
			freq1 = std::get<0>(signalpair);
			freq2 = std::get<1>(signalpair);
		}while(source && (freq1 + freq2 > mix_signal_power));

		// 再赛进去 samples_per_chip/2 个来
		// 预填充
		for(int i = 0; i < (samples_per_chip/2) && source; i++)
		{
			// 预填充 4 个信号点
			std::move(std::begin(cap)+1, std::end(cap), std::begin(cap));
			cap.back() = 0;
			Decoder( std::accumulate(std::begin(cap), std::end(cap),0) > (samples_per_chip/2) );
		}

		std::fprintf(stderr, "\r[%08d] no signal !!!!!!!!!!! sooooooooooo bad\n", t);
	}
}

// 信道编码 使用  4b5b 编码后执行冯 1 跳变调制
static void channel_encoder(boost::coroutines::asymmetric_coroutine<int>::push_type & sink)
{
	#include "4b5btable.ipp"

	while(sink)
	{
		boost::coroutines::asymmetric_coroutine<uint8_t>::pull_type packer(datapacker);

		// 把数据包进行编码

		// 首先检查是否有数据包
		if(!packer)
		{
			// 如果没有数据包，packer 就会直接退出，导致这里判断进入
			// 没有数据要发送，就不发出声音，这个是由 indeterminate_value 指示的
			// 但是这里要等 100ms
			for(int i=0; i< 100 ; i++)
				sink(2);
			continue;
		}

		int prev_bit = 0;

		auto put5b = [&prev_bit,&sink](uint8_t bits)
		{
			for( int j = 0 ; j < 5 ; j++, bits <<= 1)
			{
				int bit = bits & 0b10000;
				if(bit)
					prev_bit = !prev_bit;

				sink(prev_bit);
			}

		};

		// 有数据包了，那么循环进入

		// 首先生成一段同步序列  低低低低低高低高低高，表示 0000011111 ，
		// 这个是不会出现在任何编码结果中的序列
		put5b(0);
		put5b(FFM_encode_table[16]);

		// 接着进入 4b/5b 编码
		do{
			uint8_t byte = packer.get();
			put5b(FFM_encode_table[ byte >> 4 ]);
			put5b(FFM_encode_table[ byte & 0xF ]);
			packer();
		}while(packer);
		put5b(FFM_encode_table[0]);
		put5b(FFM_encode_table[16]);
	}
}

#include "RF_chip.hpp"

void send_packet(std::vector<uint8_t> packet)
{
	packet_send_buffer.push(packet);
}

int main(int argc, char **argv)
{
	std::vector<uint8_t> packet = { 'H', 'e', 'l', 'l', 'o', ' ', 'w', 'o', 'r', 'l', 'd' };


	std::thread Rxthread(SiFi_Rx);
	std::thread Txthread(SiFi_Tx);

	//sleep(1);

	send_packet(packet);

	Txthread.join();
	Rxthread.join();
    return 0;
}
