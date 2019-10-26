#include "SDLVideoPlayer.h"
#include <iostream>

// ffmpeg and sdl header
extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include <libavutil/imgutils.h>
#include <SDL.h>
}

SDLVideoPlayer::SDLVideoPlayer()
{
    m_demuxThread.reset(new std::thread(&SDLVideoPlayer::DoDemuex, this));
    m_videoDecodeThread.reset(new std::thread(&SDLVideoPlayer::DoDecodeVideo, this));
    m_audioDecodeThread.reset(new std::thread(&SDLVideoPlayer::DoDecodeAudio, this));
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

    if (m_sdlPlayThread && m_sdlPlayThread->joinable())
    {
        m_sdlPlayThread->join();
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

        if (m_demuxer.Open(m_filename, m_videoCodecId, m_audioCodecId) != DM_OK)
        {
            continue;
        }

        int videoIndex = m_demuxer.GetVideoStreamId();
        int audioIndex = m_demuxer.GetAudioStremId();
        lock.unlock();
        while (!m_bExitFlag && m_bStartPlay)
        {
            AVPacket *pkt = m_demuxer.GetPacket();
            if (!pkt)
            {
                m_bStartPlay = false;
                continue;
            }
            else
            {
                if (pkt->stream_index == videoIndex)
                {
                    std::lock_guard<std::mutex> lock(m_videoTaskMutex);
                    m_videoDecodeTask.push(pkt);
                    m_videoTaskCv.notify_one();
                }
                else if (pkt->stream_index == audioIndex)
                {
                    std::lock_guard<std::mutex> lock(m_audioTaskMutex);
                    m_audioDecodeTask.push(pkt);
                    m_audioTaskCv.notify_one();
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

            //decode pkt
            std::cout << pkt->pos << " : video packet" << std::endl;

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

            //next pkt
            lock.lock();
        }

        m_audioTaskCv.wait(lock);
    }
}