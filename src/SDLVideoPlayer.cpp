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
enum USER_EVNET
{
	SFM_NEW_EVENT = (SDL_USEREVENT + 1),
	SFM_REFRESH_EVENT,
	SFM_QUIT_EVENT,
};

SDLVideoPlayer::SDLVideoPlayer()
{
	m_sdlUiThread.reset(new std::thread(&SDLVideoPlayer::ShowPlayUI, this));
	m_demuxThread.reset(new std::thread(&SDLVideoPlayer::DoDemuex, this));
	m_videoDecodeThread.reset(new std::thread(&SDLVideoPlayer::DoDecodeVideo, this));
	m_audioDecodeThread.reset(new std::thread(&SDLVideoPlayer::DoDecodeAudio, this));
	m_sdlTimerThread.reset(new std::thread(&SDLVideoPlayer::StartTimer, this));
}

SDLVideoPlayer::~SDLVideoPlayer()
{
	if (!m_bExitFlag)
	{
		Free();
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

	if (m_sdlTimerThread && m_sdlTimerThread->joinable())
	{
		m_sdlTimerThread->join();
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
	m_bStartPlay = false;
	m_startCv.notify_all();
	m_videoTaskCv.notify_all();
	m_audioTaskCv.notify_all();

	if (m_imgConvertCtx)
	{
		sws_freeContext(m_imgConvertCtx);
		m_imgConvertCtx = nullptr;
	}
}

void SDLVideoPlayer::Free()
{
	m_bExitFlag = true;
	StopPlay();

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

		if (!m_bStartPlay || m_demuxer.Open(m_filename, m_videoCodecParams, m_audioCodecParams) != DM_OK)
		{
			continue;
		}

		//Open codec
		if (!m_videoDecoder.Init(&m_videoCodecParams))
		{
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
				//m_bStartPlay = false;
				break;
			}
			else
			{
				if (pkt->stream_index == videoIndex)
				{
					std::unique_lock<std::mutex> lock(m_videoTaskMutex);
					m_videoDecodeTask.push(pkt);
					m_videoTaskCv.notify_one();

					if (m_videoDecodeTask.size() > 10)
					{
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
			while (true)
			{
				AVFrame *frame = m_videoDecoder.GetDecodedFrame();
				if (frame == nullptr)
				{
					break;
				}

				std::unique_lock<std::mutex> frameLock(m_videoFrameMutex);
				m_videoFrameQueue.push(frame);

				if (m_videoFrameQueue.size() > 10)
				{ // cache
					m_videoFrameCv.wait(frameLock);
				}
			}

			av_packet_unref(pkt);
			av_packet_free(&pkt);

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

void SDLVideoPlayer::ShowPlayUI()
{
	// Init SDL
	if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER))
	{
		std::cout << "Could not init SDL subsystem" << std::endl;
		return;
	}

	m_sdlMainWindow = SDL_CreateWindow("SDLVideoPlayer", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, m_screenWidth, m_screenHight, SDL_WINDOW_RESIZABLE);
	if (!m_sdlMainWindow)
	{
		std::cout << "SDL: could not create window - exiting: " << SDL_GetError() << std::endl;
		return;
	}
    m_sdlRender = SDL_CreateRenderer(m_sdlMainWindow, -1, 0);

	SDL_Event event;
	while (!m_bExitFlag)
	{
		SDL_WaitEvent(&event);
		if (event.type == SFM_NEW_EVENT)
		{
			m_sdlMutex.lock();
			m_screenWidth = m_videoCodecParams.width;
			m_screenHight = m_videoCodecParams.height;
			SDL_SetWindowSize(m_sdlMainWindow, m_screenWidth, m_screenHight);
			m_sdlMutex.unlock();

			SDL_Event event;
			event.type = SDL_WINDOWEVENT;
			event.window.event = SDL_WINDOWEVENT_RESIZED;
			SDL_PushEvent(&event);
			// Start play
			std::lock_guard<std::mutex> lock(m_timerMutex);
			m_timerCv.notify_one();
		}
		else if (event.type == SFM_QUIT_EVENT)
		{
			SDL_Quit();
			return;
		}
		else if (event.type == SFM_REFRESH_EVENT)
		{
			// next frame
            int width, height;
            SDL_GetWindowSize(m_sdlMainWindow, &width, &height);
            if (m_screenWidth != width || m_screenHight != height) {
                SDL_Event event;
                event.type = SDL_WINDOWEVENT;
                event.window.event = SDL_WINDOWEVENT_RESIZED;
                SDL_PushEvent(&event);
            }
			PlayFrame();
		}
		else if (event.type == SDL_WINDOWEVENT)
		{
			switch (event.window.event)
			{
			case SDL_WINDOWEVENT_CLOSE:
				m_bExitFlag = true;
				m_bStartPlay = false;
				SDL_Quit();
				return;
			case SDL_WINDOWEVENT_RESIZED:
			{
				m_sdlMutex.lock();
                SDL_GetWindowSize(m_sdlMainWindow, &m_screenWidth, &m_screenHight);

				double wScaleFactor = ((double)m_screenWidth) / m_videoCodecParams.width;
				double hScaleFactor = ((double)m_screenHight) / m_videoCodecParams.height;
				if (wScaleFactor < hScaleFactor)
				{
					m_sdlRect.w = m_screenWidth;
					m_sdlRect.h = (int)(m_videoCodecParams.height * wScaleFactor);
				}
				else
				{
					m_sdlRect.w = (int)(m_videoCodecParams.width * hScaleFactor);
					m_sdlRect.h = m_screenHight;
				}

				m_sdlRect.x = (m_screenWidth - m_sdlRect.w) / 2;
				m_sdlRect.y = (m_screenHight - m_sdlRect.h) / 2;

				if (m_imgConvertCtx)
				{
					sws_freeContext(m_imgConvertCtx);
					m_imgConvertCtx = nullptr;
				}

				if (m_videoDecoder.GetCodecContext())
				{
					m_imgConvertCtx = sws_getContext(m_videoCodecParams.width, m_videoCodecParams.height, m_videoDecoder.GetCodecContext()->pix_fmt, m_sdlRect.w, m_sdlRect.h, AV_PIX_FMT_YUV420P, SWS_BICUBIC, NULL, NULL, NULL);
				}

				if (m_sdlTexture)
				{
					SDL_DestroyTexture(m_sdlTexture);
					m_sdlTexture = nullptr;
				}
				m_sdlTexture = SDL_CreateTexture(m_sdlRender, SDL_PIXELFORMAT_IYUV, SDL_TEXTUREACCESS_STREAMING, m_sdlRect.w, m_sdlRect.h);

				//SDL_RenderSetViewport(m_sdlRender, NULL);
				m_sdlMutex.unlock();
			}
			break;
			}
        }
        else if (event.type == SDL_KEYDOWN) {
            if (event.key.keysym.scancode == SDL_SCANCODE_RETURN) {
                //FullScreen
                if (SDL_GetWindowFlags(m_sdlMainWindow) & SDL_WINDOW_FULLSCREEN_DESKTOP) {
                    SDL_SetWindowFullscreen(m_sdlMainWindow, 0);
                    SDL_RestoreWindow(m_sdlMainWindow);
                    SDL_SetWindowPosition(m_sdlMainWindow, SDL_WINDOWPOS_CENTERED,SDL_WINDOWPOS_CENTERED);
                }
                else 
                {
                    SDL_SetWindowFullscreen(m_sdlMainWindow, SDL_WINDOW_FULLSCREEN_DESKTOP);
                }
            }
        }
	}
}

void SDLVideoPlayer::PlayFrame()
{
	m_videoFrameMutex.lock();
	if (m_videoFrameQueue.empty())
	{
		m_videoFrameCv.notify_one();
		m_videoFrameMutex.unlock();

		if (m_bStartPlay)
		{
			//Finish
			StopPlay();
		}
		return;
	}

	AVFrame *frame = m_videoFrameQueue.front();
	m_videoFrameQueue.pop();

	m_videoFrameCv.notify_one();
	m_videoFrameMutex.unlock();

	AVFrame *frameYUV = av_frame_alloc();
	m_sdlMutex.lock();
	int ret = av_image_alloc(frameYUV->data, frameYUV->linesize, m_sdlRect.w, m_sdlRect.h, AV_PIX_FMT_YUV420P, 1);
	//Convert image
	if (m_imgConvertCtx)
	{
		sws_scale(m_imgConvertCtx, frame->data, frame->linesize, 0, m_videoCodecParams.height, frameYUV->data, frameYUV->linesize);
		SDL_UpdateYUVTexture(m_sdlTexture, NULL, frameYUV->data[0], frameYUV->linesize[0], frameYUV->data[1], frameYUV->linesize[1], frameYUV->data[2], frameYUV->linesize[2]);
		SDL_RenderClear(m_sdlRender);
		SDL_RenderCopy(m_sdlRender, m_sdlTexture, NULL, &m_sdlRect);

		// Present picture
		SDL_RenderPresent(m_sdlRender);
	}
	m_sdlMutex.unlock();
	av_freep(&frameYUV->data[0]);
	av_frame_free(&frame);
	av_frame_free(&frameYUV);
}

void SDLVideoPlayer::StartTimer()
{
	while (!m_bExitFlag)
	{
		std::unique_lock<std::mutex> lock(m_timerMutex);
		m_timerCv.wait(lock);
		lock.unlock();

		while (!m_bExitFlag)
		{
			SDL_Event event;
			event.type = SFM_REFRESH_EVENT;
			SDL_PushEvent(&event);
			SDL_Delay(40); //25fps
		}
	}
}