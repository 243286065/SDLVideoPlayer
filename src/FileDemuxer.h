/**
* Demuxer viedo file
*/

#pragma once

#include <string>
#include <mutex>
#include <atomic>

struct AVFormatContext;
struct AVCodecContext;
struct AVPacket;

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

	DemuxerErrCode Open(const std::string &filename, AVCodecID &videoCodecId, AVCodecID &audioCodecId);
	void Close();

	AVPacket *GetPacket();

	int GetVideoStreamId() { return m_videoStreamIndex; }
	int GetAudioStremId() { return m_audioStreamIndex; }

private:
	AVFormatContext *m_pFormatCtx = nullptr;
	int m_videoStreamIndex;
	int m_audioStreamIndex;
	std::mutex m_syncMutex;

	// Check if already open a file
	std::atomic_bool m_hasOpenFile = false;
};