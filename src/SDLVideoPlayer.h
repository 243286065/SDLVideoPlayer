#pragma once

#include <string>
#include <thread>
#include <atomic>
#include <queue>

#include "FileDemuxer.h"

class SDLVideoPlayer
{
public:
	SDLVideoPlayer();
	~SDLVideoPlayer();

	void StartPlay(const std::string &filename);
	void StopPlay();

private:
	void DoDemuex();
	void DoDecodeVideo();
	void DoDecodeAudio();

	FileDemuxer m_demuxer;

	std::unique_ptr<std::thread> m_demuxThread = nullptr;
	std::unique_ptr<std::thread> m_videoDecodeThread = nullptr;
	std::unique_ptr<std::thread> m_audioDecodeThread = nullptr;
	std::unique_ptr<std::thread> m_sdlPlayThread = nullptr;
	//main thread
	//std::unique_ptr<std::thread> m_sdlUiThread;

	std::atomic_bool m_bExitFlag = false;
	std::atomic_bool m_bStartPlay = false;
	std::string m_filename;
	std::mutex m_syncMutex;
	std::condition_variable m_startCv;

	std::queue<AVPacket *> m_videoDecodeTask;
	std::queue<AVPacket *> m_audioDecodeTask;
	std::mutex m_videoTaskMutex;
	std::condition_variable m_videoTaskCv;
	std::mutex m_audioTaskMutex;
	std::condition_variable m_audioTaskCv;

	AVCodecID m_videoCodecId;
	AVCodecID m_audioCodecId;
};