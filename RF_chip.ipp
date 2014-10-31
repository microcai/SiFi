#define CHIPSIZE 960
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
		//	demodulator(s);
			// 如果 FSK 调制器挂了，就别输出啦！
			if(!samples)
				break;
		}
		// 写入声卡播放缓冲区
		pa.write(boost::asio::buffer(buf));
	}
	// RAII 自动关闭了
}

