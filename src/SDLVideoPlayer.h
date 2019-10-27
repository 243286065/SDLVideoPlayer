#pragma once

#include <string>
#include <thread>
#include <atomic>
#include <queue>

#include "FileDemuxer.h"
#include "VideoDecoder.h"

extern "C"
{
#include <libavcodec/avcodec.h>
#include <SDL.h>
}

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
    void ShowPlayUI();

	FileDemuxer m_demuxer;
    VideoDecoder m_videoDecoder;

	std::unique_ptr<std::thread> m_demuxThread = nullptr;
	std::unique_ptr<std::thread> m_videoDecodeThread = nullptr;
	std::unique_ptr<std::thread> m_audioDecodeThread = nullptr;
    std::unique_ptr<std::thread> m_sdlUiThread = nullptr;
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

    AVCodecParameters m_videoCodecParams;
    AVCodecParameters m_audioCodecParams;

    //SDL
    SDL_Window *m_sdlMainWindow;
    SDL_Renderer *m_sdlRender;
    SDL_Texture *m_sdlTexture;
    SDL_Rect m_sdlRect;
};