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

struct SwsContext;

class SDLVideoPlayer
{
public:
	SDLVideoPlayer();
	~SDLVideoPlayer();

	void StartPlay(const std::string &filename);
	void StopPlay();
    void Free();

private:
	void DoDemuex();
	void DoDecodeVideo();
	void DoDecodeAudio();
    void ShowPlayUI();

    void PlayFrame();
    void StartTimer();

	FileDemuxer m_demuxer;
    VideoDecoder m_videoDecoder;

	std::unique_ptr<std::thread> m_demuxThread = nullptr;
	std::unique_ptr<std::thread> m_videoDecodeThread = nullptr;
	std::unique_ptr<std::thread> m_audioDecodeThread = nullptr;
    std::unique_ptr<std::thread> m_sdlUiThread = nullptr;
    std::unique_ptr<std::thread> m_sdlTimerThread = nullptr;
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
    std::queue<AVFrame *> m_videoFrameQueue;
    std::queue<AVFrame *> m_audioFrameQueue;
    std::mutex m_videoFrameMutex;
    std::mutex m_audioFrameMutex;
    std::condition_variable m_videoFrameCv;
    std::condition_variable m_audioFrameCv;
    SwsContext* m_imgConvertCtx = nullptr;
    std::queue<double> m_videoFrameTimestampQueue;
    std::queue<double> m_audioFrameTimestampQueue;
    int64_t m_mainTimestamp;

    std::mutex m_timerMutex;
    std::condition_variable m_timerCv;

    //SDL
    SDL_Window *m_sdlMainWindow = nullptr;
    SDL_Renderer *m_sdlRender = nullptr;
    SDL_Texture *m_sdlTexture = nullptr;
    SDL_Rect m_sdlRect;
    std::mutex m_sdlMutex;
    int m_screenWidth = 800;
    int m_screenHight = 600;

    std::atomic_bool m_bDemuxFinish = false;
};