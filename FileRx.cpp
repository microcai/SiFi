
extern  "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avconfig.h>
}

static int t = 0;

#include "RF_chip.hpp"

// 文件测试接收
void File_Rx()
{
	av_register_all();

    boost::coroutines::asymmetric_coroutine<float>::push_type demodulator(FSK_demodulator<CHIPSIZE/2,CHIPSIZE, SAMPLES_PER_CHIP>);

    AVFormatContext * avcontext = avformat_alloc_context();

    if(avformat_open_input(&avcontext, "test.wav", NULL, NULL))
    {
        std::cerr << "can't open test.wav" << std::endl;
        exit(0);
    }

    if( avformat_find_stream_info(avcontext, NULL) )
    {
        std::cerr << "can't guess test.wav file format" << std::endl;
        exit(0);
    }

    AVCodec * decoder = NULL;
	int audio_stream_index = av_find_best_stream(avcontext, AVMEDIA_TYPE_AUDIO, -1, -1, &decoder, 0);

    if( avcodec_open2(avcontext->streams[audio_stream_index]->codec, decoder, NULL) )
    {
        std::cerr << "avcodec_open2 failed" << std::endl;
        exit(0);
    }

	// 循环读取
    while(true)
    {
		AVPacket packet = { 0 };
		av_init_packet(&packet);
		BOOST_SCOPE_EXIT_ALL(&packet)
		{
			av_free_packet(&packet);
		};

		// finished
		if( av_read_frame(avcontext, &packet) < 0 )
		{
			std::cerr << "file EOF" << std::endl;
			break;
		}

		if( packet.stream_index != audio_stream_index)
			continue;

		AVFrame * avframe = av_frame_alloc();
		BOOST_SCOPE_EXIT_ALL(&avframe)
		{
			av_frame_free(&avframe);
		};

		while(1)
		{
			int got_frame = 0;
			int ret = avcodec_decode_audio4(avcontext->streams[audio_stream_index]->codec, avframe, &got_frame, &packet);
			if( ret < 0 )
			{
				std::cerr << "error decoding audio file" << std::endl;
				exit(1);
			}

			packet.size -= ret;
			packet.data += ret;
			if (!got_frame && packet.size > 0)
				continue;

			/* packet中已经没有数据了, 并且不足一个帧, 丢弃这个音频packet. */
			if (packet.size == 0 && !got_frame)
				break;

			if (avframe->linesize[0] != 0)
			{
				// 传递给流水线下一步
				for(int i=0; i < av_samples_get_buffer_size(NULL,
					avcontext->streams[audio_stream_index]->codec->channels, avframe->nb_samples,
						avcontext->streams[audio_stream_index]->codec->sample_fmt,1) ; i+=2)
				{
					// 丢掉一个频道
					demodulator( avframe->data[0][i] / 32768.0 );
				}
			}
		}
	}

    for(int i=0; i < avcontext->nb_streams; i ++)
    {
        if(avcontext->streams[i]->codec)
            avcodec_close(avcontext->streams[i]->codec);
    }

    avformat_close_input(&avcontext);
    // RAII 自动关闭了
}

