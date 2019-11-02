#pragma once

#include <string>
#include <thread>
#include <atomic>
#include <queue>

#include "FileDemuxer.h"
#include "PacketDecoder.h"

extern "C"
{
#include <libavcodec/avcodec.h>
#include <SDL.h>
}

struct SwsContext;
struct SwrContext;

enum StreamType {
    Video,
    Audio,
};

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
    void ShowPlayAudio();

    void PlayPictureFrame();
    void ReadAudioFrame(AVFrame *frame);
    void StartPictureTimer();

    static void ReadAudioData(void *udata, Uint8 *stream, int len);

    void GetPts(StreamType type, double currentPts, double &nextPts);
    char* GetErrorInfo(const int code);

	FileDemuxer m_demuxer;
    PacketDecoder m_videoDecoder;
    PacketDecoder m_audioDecoder;

	std::unique_ptr<std::thread> m_demuxThread = nullptr;
	std::unique_ptr<std::thread> m_videoDecodeThread = nullptr;
	std::unique_ptr<std::thread> m_audioDecodeThread = nullptr;
    std::unique_ptr<std::thread> m_sdlUiThread = nullptr;
    std::unique_ptr<std::thread> m_sdlVideoTimerThread = nullptr;
    std::unique_ptr<std::thread> m_sdlAudioThread = nullptr;
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
    SwrContext* m_audioConvertCtx = nullptr;
    std::queue<double> m_videoFrameTimestampQueue;
    std::queue<double> m_audioFrameTimestampQueue;

    std::mutex m_timerMutex;
    std::condition_variable m_timerCv;

    //SDL
    SDL_Window *m_sdlMainWindow = nullptr;
    SDL_Renderer *m_sdlRender = nullptr;
    SDL_Texture *m_sdlTexture = nullptr;
    SDL_Rect m_sdlRect;
    SDL_AudioSpec m_sdlAudioSpec;
    std::mutex m_sdlMutex;
    int m_screenWidth = 800;
    int m_screenHight = 600;

    std::atomic_bool m_bDemuxFinish = false;
    int64_t m_mousetLastActiveTs;

    char m_buf[1024];
    static uint8_t* m_audioPcmDataBuf;
    static int m_audioPcmBufLen;
    static std::mutex m_audioPcmMutex;
    static std::condition_variable m_pcmCvRead;
    static std::condition_variable m_pcmCvWrite;
    static double m_currentPts;
};