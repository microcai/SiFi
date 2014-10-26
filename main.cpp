
#include <cmath>
#include <iostream>
#include <algorithm>
#include <bitset>
#include <array>
#include <thread>
#include <functional> 
#include <boost/asio/buffer.hpp>
#include <boost/math/constants/constants.hpp>
#include <fftw3.h>
#include <boost/coroutine/asymmetric_coroutine.hpp>

#include "dqueue.hpp"
#include "pulseaudio.hpp"

static const int samplerate = 48000;

template <class T>
T circumference(T r)
{
   return boost::math::constants::two_pi<T>() * r;
}

static std::array<int,3> target_frequency[] =
{
	{ 1200 , 2200 ,0},

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
				pipeline_feed(FFM_decode_table[codegroup.to_ulong()]);
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
		if( abscount >= 45)
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
static void BaseBand_Filter(boost::coroutines::asymmetric_coroutine<std::tuple<double,double>>::pull_type & source)
{
	while(source)
	{
		auto signalpair = source.get();
		source();

		auto freq1 = std::get<0>(signalpair);
		auto freq2 = std::get<1>(signalpair);

		if( freq1 + freq2 < 4 )
		{
			std::fprintf(stderr, "\r[%08d] no signal", t);
			continue;
		}
		
		std::array<uint8_t, 10> cap;
		std::fill(std::begin(cap), std::end(cap),0);

		// 预填充
		for(int i = 10 - 5; i < 10 && source && (freq1 + freq2 > 4); i++)
		{			
			// 预填充 4 个信号点
			cap[i] = (freq1 < freq2);

			signalpair = source.get(); source();
			freq1 = std::get<0>(signalpair);
			freq2 = std::get<1>(signalpair);
		}

		if( freq1 + freq2 < 4 )
		{
			std::fprintf(stderr, "\r[%08d] no signal", t);
			continue;
		}

		// 建立一个带通滤波解码器
		boost::coroutines::asymmetric_coroutine<bool>::push_type Decoder(BaseBand_Decode);

		do
		{
			std::move(std::begin(cap)+1, std::end(cap), std::begin(cap));
			cap.back() = (freq1 < freq2);

			// 整理出 10 个信号点并平均化
			Decoder( std::accumulate(std::begin(cap), std::end(cap),0) >= 5 );

			signalpair = source.get();
			source();
			freq1 = std::get<0>(signalpair);
			freq2 = std::get<1>(signalpair);
		}while(source && (freq1 + freq2 > 4));

		// 再赛进去 5 个来
				// 预填充
		for(int i = 0; i < 5 && source; i++)
		{
			// 预填充 4 个信号点
			std::move(std::begin(cap)+1, std::end(cap), std::begin(cap));
			cap.back() = 0;
			Decoder( std::accumulate(std::begin(cap), std::end(cap),0) >= 5 );
		}
	}
}

#include "Goertzel.hpp"

template<unsigned windowssize, unsigned incremental>
static void FSK_demodulator(boost::coroutines::asymmetric_coroutine<float>::pull_type & source)
{
	std::array<float, windowssize> Samples;

	const std::array<float, windowssize> Hammingwindow = [](){
		std::array<float, windowssize> r;
		for(int i=0; i < r.size(); i++)
		{
			r[i] = 0.5 * (
				1 - std::cos( circumference<double>(i) / (r.size() -1) )
			);
		}
		return r;
	}();

	boost::coroutines::asymmetric_coroutine<std::tuple<double,double>>::push_type baseband_filter(BaseBand_Filter);

	while(source)
	{
		std::array<float, windowssize> CheckWindow = { 0 };

		// 移动 incremental 个采样点	
		std::move(std::begin(Samples)+incremental, std::end(Samples), std::begin(Samples));
		for(unsigned i = windowssize - incremental; i < windowssize ; i++)
		{
			Samples[i] = source.get();
			source();
		}
		// 加 Hammingwindow	
		std::transform(std::begin(Samples), std::end(Samples),
			std::begin(Hammingwindow), std::begin(CheckWindow), [](auto a, auto b){return a * b;});

		// 执行检测
		auto freq1 = Goertzel_frequency_detector<windowssize>(CheckWindow, freq_selector()[0], samplerate);
		auto freq2 = Goertzel_frequency_detector<windowssize>(CheckWindow, freq_selector()[1], samplerate);

		// NOTE: 向流水线下游传递，从频点信号中恢复基带信号
		t++;
		baseband_filter(std::make_tuple(freq1,freq2));
	}
}

// 接收器
static void SiFi_Rx()
{
	static const pa_sample_spec ss = {
		PA_SAMPLE_S16LE,
		samplerate,
		1
	};

	boost::coroutines::asymmetric_coroutine<float>::push_type demodulator(FSK_demodulator<240,80>);

	// 打开音频设备	
	pa_simple pa(NULL, "SiFi", PA_STREAM_RECORD, NULL, "receive SiFi signals", &ss, NULL, NULL);

	// 循环读取
	while(true)
	{
		std::array<int16_t, 480> buf = { 0 };
		auto  readed = pa.read(boost::asio::buffer(buf, buf.size()));
		
		BOOST_VERIFY(readed == 0);

		// 传递给流水线下一步
		std::for_each(std::begin(buf),std::end(buf),[&demodulator](int16_t v){
			demodulator( v / 32768.0 );
		});
	}
	// RAII 自动关闭了
}

static dqueue<5, std::array<uint8_t, 11>> packet_send_buffer;

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

// FFM 调制编码器 ( Four to Five modulation)
static void FFM_encoder(boost::coroutines::asymmetric_coroutine<int>::push_type & sink)
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
	}
}

//  FSK 调制器
static void FSK_modulator(boost::coroutines::asymmetric_coroutine<double>::push_type & sink)
{
	const double TWO_PI = 4 * std::asin(1.0);
	double last_sample = 0.0;

	boost::coroutines::asymmetric_coroutine<int>::pull_type encoder(FFM_encoder);

	// 循环读取
	do
	{
		// 读入一个比特
		int modulation_bit = encoder.get();

		// 生成一段时间的正玄波信号, 如果没有信号就会生成一段空白。
		double desired_freq = freq_selector() [modulation_bit];

		// 生成正玄波， 用三角函数算呗！
		// 根据频率和采样率，计算采样点坐标
		for(int i=0; i < 960 ; i++)
		{
			double s = std::sin( TWO_PI * (last_sample));
			last_sample += desired_freq / samplerate;
			// 吐出生成数据给流水线下一条
			sink(s);
		}
			// 圆整到 [0,1]
		last_sample -= (long)last_sample;
		if(last_sample > 0.999999)
				last_sample = 0;
		encoder();
	}while(encoder);
}

// 发射部分代码
static void SiFi_Tx()
{
	static const pa_sample_spec ss = {
		PA_SAMPLE_S16LE,
		samplerate,
		1
	};

	int error;
	// 打开音频设备	
	pa_simple pa(NULL, "SiFi", PA_STREAM_PLAYBACK, NULL, "transmit SiFi signals", &ss, NULL, NULL);
	
	pa_simple_flush(pa.get(), &error);
	
	boost::coroutines::asymmetric_coroutine<double>::pull_type samples(FSK_modulator);

	boost::coroutines::asymmetric_coroutine<float>::push_type demodulator(FSK_demodulator<480,96>);

	double last_sample = 0.0;
	// 循环写入
	while(samples)
	{
		// 获取 signals_sample_queue 列队。格式化为声卡支持的 S16 或者 FLOAT32LE 格式
		int16_t buf[2048] = { 0 };

		// 从FSK调制器拿到正玄波数据
		for(auto & s : buf)
		{
			// FSK 调制器输出为 浮点数，转换为 short 格式。
			s = samples.get() * 32000;

			samples();
			
			// 如果 FSK 调制器挂了，就别输出啦！
			if(!samples)
				break;
		}
		// 写入声卡播放缓冲区
		pa.write(boost::asio::buffer(buf));
	}
	// RAII 自动关闭了
}

int main(int argc, char **argv)
{	
	std::array<uint8_t,11> packet = { 'H', 'e', 'l', 'l', 'o', ' ', 'w', 'o', 'r', 'l', 'd' };
	
	packet_send_buffer.push(packet);
	
	std::thread Rxthread(SiFi_Rx);
	std::thread Txthread(SiFi_Tx);
	Txthread.join();
	Rxthread.join();	
    return 0;
}
