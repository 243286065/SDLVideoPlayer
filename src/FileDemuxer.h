/**
* Demuxer viedo file
*/

#pragma once

extern "C"
{
#include <libavutil/rational.h>
}

#include <string>
#include <mutex>
#include <atomic>

struct AVFormatContext;
struct AVCodecContext;
struct AVPacket;
struct AVCodecParameters;

enum DemuxerErrCode
{
	DM_OK = 0,
	DM_ALREADY_OPEN,
	DM_OPEN_INPUT_FAILED,
	DM_FIND_STREM_FAILED,
	DM_FIND_VIDEO_STREM_FAILED,
	DM_FIND_VIDEO_DECODER_FAILED,
	DM_FIND_AUDIO_STREM_FAILED,
	DM_FIND_AUDIO_DECODER_FAILED,
};

enum AVCodecID;

class FileDemuxer
{
public:
	FileDemuxer();
	~FileDemuxer();

	DemuxerErrCode Open(const std::string &filename, AVCodecParameters &videoCodec, AVCodecParameters &audioCodec);
	void Close();

	AVPacket *GetPacket();

	int GetVideoStreamId() { return m_videoStreamIndex; }
	int GetAudioStremId() { return m_audioStreamIndex; }

    double GetVideoTotalSecond() { return m_videoTotalSecond; }
    double GetAudioTotalSecond() { return m_audioTotalSecond; }
    AVRational GetVideoTimeBase();
    AVRational GetAudioTimeBase();

    void SeekFrame(int pts);
private:
	AVFormatContext *m_pFormatCtx = nullptr;
	int m_videoStreamIndex;
	int m_audioStreamIndex;
	std::mutex m_syncMutex;
    double m_videoTotalSecond = 0;
    double m_audioTotalSecond = 0;

	// Check if already open a file
	std::atomic_bool m_hasOpenFile = false;
};