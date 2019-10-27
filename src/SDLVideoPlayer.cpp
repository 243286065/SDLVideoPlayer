#include "SDLVideoPlayer.h"
#include <iostream>

// ffmpeg and sdl header
extern "C"
{
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include <libavutil/imgutils.h>
}


//Refresh Event
enum USER_EVNET {
    SFM_NEW_EVENT = (SDL_USEREVENT + 1),
    SFM_REFRESH_EVENT,
    SFM_QUIT_EVENT,
};

SDLVideoPlayer::SDLVideoPlayer()
{
    m_demuxThread.reset(new std::thread(&SDLVideoPlayer::DoDemuex, this));
    m_videoDecodeThread.reset(new std::thread(&SDLVideoPlayer::DoDecodeVideo, this));
    m_audioDecodeThread.reset(new std::thread(&SDLVideoPlayer::DoDecodeAudio, this));
    m_sdlUiThread.reset(new std::thread(&SDLVideoPlayer::ShowPlayUI, this));
}

SDLVideoPlayer::~SDLVideoPlayer()
{
    if (m_bStartPlay)
    {
        StopPlay();
    }

    if (m_demuxThread && m_demuxThread->joinable())
    {
        m_demuxThread->join();
    }

    if (m_videoDecodeThread && m_videoDecodeThread->joinable())
    {
        m_videoDecodeThread->join();
    }

    if (m_audioDecodeThread && m_audioDecodeThread->joinable())
    {
        m_audioDecodeThread->join();
    }

    if (m_sdlUiThread && m_sdlUiThread->joinable())
    {
        m_sdlUiThread->join();
    }
}

void SDLVideoPlayer::StartPlay(const std::string &filename)
{
    std::lock_guard<std::mutex> lock(m_syncMutex);
    if (m_bStartPlay)
    {
        std::cout << "One video file is on playing, please first close that" << std::endl;
        return;
    }

    m_filename = filename;
    m_bStartPlay = true;
    // Start to demux file
    m_startCv.notify_one();
}

void SDLVideoPlayer::StopPlay()
{
    m_bExitFlag = true;
    m_bStartPlay = true;
    m_startCv.notify_all();
    m_videoTaskCv.notify_all();
    m_audioTaskCv.notify_all();

    SDL_Event event;
    event.type = SFM_QUIT_EVENT;
    SDL_PushEvent(&event);
}

void SDLVideoPlayer::DoDemuex()
{
    while (!m_bExitFlag)
    {
        std::unique_lock<std::mutex> lock(m_syncMutex);

        m_startCv.wait(lock);
        if (m_bExitFlag)
        {
            return;
        }

        if (m_demuxer.Open(m_filename, m_videoCodecParams, m_audioCodecParams) != DM_OK)
        {
            continue;
        }

        //Open codec
        if(!m_videoDecoder.Init(&m_videoCodecParams)) {
            continue;
        }

        //if(!m_audio)

        int videoIndex = m_demuxer.GetVideoStreamId();
        int audioIndex = m_demuxer.GetAudioStremId();

        SDL_Event event;
        event.type = SFM_NEW_EVENT;
        SDL_PushEvent(&event);

        lock.unlock();
        while (!m_bExitFlag && m_bStartPlay)
        {
            AVPacket *pkt = m_demuxer.GetPacket();
            if (!pkt)
            {
                m_bStartPlay = false;
                break;
            }
            else
            {
                if (pkt->stream_index == videoIndex)
                {
                    std::unique_lock<std::mutex> lock(m_videoTaskMutex);
                    m_videoDecodeTask.push(pkt);
                    m_videoTaskCv.notify_one();

                    if (m_videoDecodeTask.size() > 10) {
                        m_videoTaskCv.wait(lock);
                    }
                }
                else if (pkt->stream_index == audioIndex)
                {
                    /*std::lock_guard<std::mutex> lock(m_audioTaskMutex);
                    m_audioDecodeTask.push(pkt);
                    m_audioTaskCv.notify_one();*/
                }
                else
                {
                    std::cout << "Unkonw stream" << std::endl;
                }
            }
        }
    }
}

void SDLVideoPlayer::DoDecodeVideo()
{
    while (!m_bExitFlag)
    {
        std::unique_lock<std::mutex> lock(m_videoTaskMutex);
        while (!m_videoDecodeTask.empty())
        {
            AVPacket *pkt = m_videoDecodeTask.front();
            m_videoDecodeTask.pop();
            lock.unlock();
            m_videoTaskCv.notify_one();

            //decode pkt
            std::cout << pkt->pos << " : video packet" << std::endl;
            m_videoDecoder.DecodePacket(pkt);
            while (true) {
                AVFrame* frame = m_videoDecoder.GetDecodedFrame();
                if (frame == nullptr) {
                    break;
                }

                //frame->
            }

            av_packet_unref(pkt);
            //next pkt
            lock.lock();
        }

        m_videoTaskCv.wait(lock);
    }
}

void SDLVideoPlayer::DoDecodeAudio()
{
    while (!m_bExitFlag)
    {
        std::unique_lock<std::mutex> lock(m_audioTaskMutex);
        while (!m_audioDecodeTask.empty())
        {
            AVPacket *pkt = m_audioDecodeTask.front();
            m_audioDecodeTask.pop();
            lock.unlock();

            //decode pkt
            std::cout << pkt->pos << " : audio packet" << std::endl;

            av_packet_unref(pkt);
            //next pkt
            lock.lock();
        }

        m_audioTaskCv.wait(lock);
    }
}

void SDLVideoPlayer::ShowPlayUI() {
    // Init SDL
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER)) {
        std::cout << "Could not init SDL subsystem" << std::endl;
        return;
    }

    m_sdlMainWindow = SDL_CreateWindow("SDLVideoPlayer", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, 800, 600, SDL_WINDOW_RESIZABLE);
    if (!m_sdlMainWindow) {
        std::cout << "SDL: could not create window - exiting: " << SDL_GetError() << std::endl;
        return;
    }

    m_sdlRender = SDL_CreateRenderer(m_sdlMainWindow, -1, 0);

    SDL_Event event;
    while (!m_bExitFlag) {
        SDL_WaitEvent(&event);
        if (event.type == SFM_NEW_EVENT) {
            SDL_SetWindowSize(m_sdlMainWindow, m_videoCodecParams.width, m_videoCodecParams.height);
            m_sdlTexture = SDL_CreateTexture(m_sdlRender, SDL_PIXELFORMAT_IYUV, SDL_TEXTUREACCESS_STREAMING, m_videoCodecParams.width, m_videoCodecParams.height);
            m_sdlRect.x = 0;
            m_sdlRect.y = 0;
            m_sdlRect.w = m_videoCodecParams.width;
            m_sdlRect.h = m_videoCodecParams.height;
        }
        else if (event.type == SFM_QUIT_EVENT) {
            SDL_Quit();
            return;
        }
    }
}


//int SDLVideoPlayer::sfp_refresh_thread(void *opaque) {
//    thread_exit = 0;
//    thread_pause = 0;
//
//    while (!thread_exit) {
//        if (!thread_pause) {
//            SDL_Event event;
//            event.type = SFM_REFRESH_EVENT;
//            SDL_PushEvent(&event);
//        }
//        SDL_Delay(40);
//    }
//    thread_exit = 0;
//    thread_pause = 0;
//    //Break
//    SDL_Event event;
//    event.type = SFM_BREAK_EVENT;
//    SDL_PushEvent(&event);
//
//    return 0;
//}