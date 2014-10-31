#include <boost/scope_exit.hpp>
#include <fftw3.h>

// 测量噪音信号强度
template<unsigned windowssize>
static double NoiseDetect(const std::array<float, windowssize> & test_window)
{
//    auto out = (fftw_complex *)fftw_malloc(sizeof(fftw_complex) * windowssize);

	double in[windowssize];
	fftw_complex out[windowssize];

	std::copy(std::begin(test_window),std::end(test_window), in);


	auto p = fftw_plan_dft_r2c_1d(windowssize, in, out, FFTW_ESTIMATE);

	BOOST_SCOPE_EXIT_ALL(&p){
		fftw_destroy_plan(p);
	};

	fftw_execute(p);

	std::array<float, windowssize> powers;
//	std::transform(std::begin(test_window), std::end(test_window), std::begin(powers), [](auto a){return a*a;});
	std::transform(out, out + windowssize, std::begin(powers), [](auto a){return a[0]*a[0];});
	return std::accumulate(std::begin(powers), std::end(powers), 0.0) ;
	return std::log10( std::accumulate(std::begin(powers), std::end(powers), 0.0) );
}

#include "Goertzel.hpp"

template<unsigned windowssize, unsigned chipsize, unsigned samples_per_chip>
static void FSK_demodulator(boost::coroutines::asymmetric_coroutine<float>::pull_type & source)
{
	std::array<float, windowssize> Samples;

	auto const incremental = chipsize / samples_per_chip;

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

	boost::coroutines::asymmetric_coroutine<std::tuple<double,double,double>>::push_type baseband_filter(baseband_filter<samples_per_chip>);

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
		// 测量噪音水平
		auto NoiseLevel = NoiseDetect<windowssize>(CheckWindow);

		// 测量两个频点的能量
		auto freq1 = Goertzel_frequency_detector<windowssize>(CheckWindow, freq_selector()[0], samplerate);
		auto freq2 = Goertzel_frequency_detector<windowssize>(CheckWindow, freq_selector()[1], samplerate);

		auto snr = -100000.0;

		if( ( NoiseLevel - freq1 - freq2) != 0.0 )
			snr = std::log10( freq1 + freq2 / ( NoiseLevel - freq1 - freq2) );
		else if( NoiseLevel > 0)
			snr = 0.0;
		// NOTE: 向流水线下游传递，从频点信号中恢复基带信号
		t++;
		baseband_filter(std::make_tuple(freq1,freq2, snr));
	}
}

// 加一个窗口
template<unsigned chipsize, unsigned guardgap>
static void chipwindow(boost::coroutines::asymmetric_coroutine<double>::push_type & sink)
{
	for( int i = 0 ;  i < guardgap/2; i++)
	{
		sink ( 0.5001 - std::cos( circumference( (double)i / (double)guardgap) ) /2 );
	}

	for(int i = 0; i < chipsize - guardgap ; i ++)
		sink(1.0);

	for( int i = 0 ;  i < guardgap/2; i++)
	{
		sink ( 0.5001 + std::cos( circumference( (double)i / (double)guardgap) ) /2 );
	}

	while(true)
		sink(0.0);
}

//  FSK 调制器, 有码间留空的版本
template<unsigned chipsize, unsigned guardgap>
static void FSK_modulator(boost::coroutines::asymmetric_coroutine<double>::push_type & sink)
{
	const double TWO_PI = 4 * std::asin(1.0);
	double last_sample = 0.0;

	boost::coroutines::asymmetric_coroutine<int>::pull_type encoder(channel_encoder);

	auto chipsamplesize = chipsize - guardgap;

	// 循环读取
	do
	{

		boost::coroutines::asymmetric_coroutine<double>::pull_type chipwindow(chipwindow<chipsize, guardgap>);

		// 读入一个比特
		int modulation_bit = encoder.get();

		// 生成一段时间的正玄波信号, 如果没有信号就会生成一段空白。
		double desired_freq = freq_selector() [modulation_bit];

		// 生成正玄波， 用三角函数算呗！
		// 根据频率和采样率，计算采样点坐标
		for(int i=0; i < chipsize ; i++, chipwindow())
		{
			double s = std::sin( TWO_PI * (last_sample));
			last_sample += desired_freq / samplerate;
			// 加窗后吐出生成数据给流水线下一条
			sink(s * chipwindow.get());
		}

		// 圆整到 [0,1]
		last_sample -= (long)last_sample;
		if(last_sample > 0.999999)
				last_sample = 0;
		encoder();
	}while(encoder);
}

#define CHIPSIZE 480
#define GAPSIZE 96
#define SAMPLES_PER_CHIP 10

// 接收器
static void SiFi_Rx()
{
	static const pa_sample_spec ss = {
		PA_SAMPLE_S16LE,
		samplerate,
		2
	};

	boost::coroutines::asymmetric_coroutine<float>::push_type demodulator(FSK_demodulator<CHIPSIZE/2,CHIPSIZE, SAMPLES_PER_CHIP>);

	// 打开音频设备
	pa_simple pa(NULL, "SiFi", PA_STREAM_RECORD, NULL, "receive SiFi signals", &ss, NULL, NULL);

	// 循环读取
	while(true)
	{
		std::array<int16_t, 2048> buf = { 0 };
		auto  readed = pa.read(boost::asio::buffer(buf, buf.size()));

		if(readed != 0)
		{
			exit(1);
		}

		// 传递给流水线下一步
		for(int i=0; i < buf.size(); i+=2)
		{
			// 丢掉一个频道
			demodulator( buf[i] / 32768.0 );
		}
	}
	// RAII 自动关闭了
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

	boost::coroutines::asymmetric_coroutine<double>::pull_type samples(FSK_modulator<CHIPSIZE, GAPSIZE>);

	boost::coroutines::asymmetric_coroutine<float>::push_type demodulator(FSK_demodulator<CHIPSIZE/2,CHIPSIZE, SAMPLES_PER_CHIP>);

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
			s = samples.get() * 32000 + 0.5;

			samples();
//			demodulator(s * 0.01);
			// 如果 FSK 调制器挂了，就别输出啦！
			if(!samples)
				break;
		}
		// 写入声卡播放缓冲区
		pa.write(boost::asio::buffer(buf));
	}
	// RAII 自动关闭了
}

