#include "FileDemuxer.h"
#include <iostream>

extern "C"
{
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>
#include <libavutil/rational.h>
}

FileDemuxer::FileDemuxer() {}

FileDemuxer::~FileDemuxer()
{
	Close();
}

DemuxerErrCode FileDemuxer::Open(const std::string &filename, AVCodecParameters &videoCodec, AVCodecParameters &audioCodec)
{
	if (m_hasOpenFile)
	{
		std::cout << "Already open one file before, please close first!" << std::endl;
		return DM_ALREADY_OPEN;
	}

	std::lock_guard<std::mutex> lock(m_syncMutex);
	m_pFormatCtx = avformat_alloc_context();
	if (avformat_open_input(&m_pFormatCtx, filename.c_str(), NULL, NULL) != 0)
	{
		std::cout << "Failed to open file: " << filename << std::endl;
		Close();
		return DM_OPEN_INPUT_FAILED;
	}
	m_hasOpenFile = true;

	if (avformat_find_stream_info(m_pFormatCtx, NULL) < 0)
	{
		std::cout << "Failed to find media stream in file : " << filename << std::endl;
		Close();
		return DM_FIND_STREM_FAILED;
	}

	m_videoStreamIndex = av_find_best_stream(m_pFormatCtx, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0);
	m_audioStreamIndex = av_find_best_stream(m_pFormatCtx, AVMEDIA_TYPE_AUDIO, -1, -1, NULL, 0);
	m_videoTotalSecond = m_pFormatCtx->streams[m_videoStreamIndex]->duration * av_q2d(m_pFormatCtx->streams[m_videoStreamIndex]->time_base);
	m_audioTotalSecond = m_pFormatCtx->streams[m_audioStreamIndex]->duration * av_q2d(m_pFormatCtx->streams[m_audioStreamIndex]->time_base);

	if (m_videoStreamIndex == AVERROR_STREAM_NOT_FOUND)
	{
		std::cout << "Failed to find video stream in file : " << filename << std::endl;
		Close();
		return DM_FIND_VIDEO_STREM_FAILED;
	}

	if (m_videoStreamIndex == AVERROR_DECODER_NOT_FOUND)
	{
		std::cout << "Failed to find video decoder in file : " << filename << std::endl;
		Close();
		return DM_FIND_VIDEO_DECODER_FAILED;
	}

	if (m_audioStreamIndex == AVERROR_STREAM_NOT_FOUND)
	{
		std::cout << "Failed to find audio stream in file : " << filename << std::endl;
		Close();
		return DM_FIND_AUDIO_STREM_FAILED;
	}

	if (m_audioStreamIndex == AVERROR_DECODER_NOT_FOUND)
	{
		std::cout << "Failed to find audio decoder in file : " << filename << std::endl;
		Close();
		return DM_FIND_AUDIO_DECODER_FAILED;
	}

	//Get codec context
	videoCodec = *(m_pFormatCtx->streams[m_videoStreamIndex]->codecpar);
	audioCodec = *(m_pFormatCtx->streams[m_audioStreamIndex]->codecpar);

	av_dump_format(m_pFormatCtx, 0, filename.c_str(), 0);

	return DM_OK;
}

void FileDemuxer::Close()
{
	std::lock_guard<std::mutex> lock(m_syncMutex);
	if (m_hasOpenFile && m_pFormatCtx)
	{
		avformat_close_input(&m_pFormatCtx);
		m_hasOpenFile = false;
	}

	if (m_pFormatCtx)
	{
		avformat_free_context(m_pFormatCtx);
		m_pFormatCtx = nullptr;
	}
}

AVPacket *FileDemuxer::GetPacket()
{
	AVPacket *pkt = av_packet_alloc();
	if (av_read_frame(m_pFormatCtx, pkt) != 0)
	{
		std::cout << "Failed or finish to read frame" << std::endl;
		return nullptr;
	}
	return pkt;
}

AVRational FileDemuxer::GetVideoTimeBase()
{
	return m_pFormatCtx->streams[m_videoStreamIndex]->time_base;
}

AVRational FileDemuxer::GetAudioTimeBase()
{
	return m_pFormatCtx->streams[m_audioStreamIndex]->time_base;
}