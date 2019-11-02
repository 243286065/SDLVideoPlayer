#include "SDLVideoPlayer.h"
#include <iostream>
#include <string.h>

// ffmpeg and sdl header
extern "C"
{
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>
#include <libavutil/imgutils.h>
}

//Refresh Event
enum USER_EVNET
{
	SFM_NEW_EVENT = (SDL_USEREVENT + 1),
	SFM_REFRESH_PIC_EVENT,
    SFM_REFRESH_AUDIO_EVENT,
	SFM_QUIT_EVENT,
};

int64_t GetMillSecondsTimestamp()
{
	std::chrono::milliseconds ms =
			std::chrono::duration_cast<std::chrono::milliseconds>(
					std::chrono::system_clock::now().time_since_epoch());
	return ms.count();
}

#define MAX_PAM_BUF_SIZE 102400

uint8_t* SDLVideoPlayer::m_audioPcmDataBuf = new uint8_t[MAX_PAM_BUF_SIZE];
int SDLVideoPlayer::m_audioPcmBufLen = 0;
std::mutex SDLVideoPlayer::m_audioPcmMutex;
std::condition_variable SDLVideoPlayer::m_pcmCvRead;
std::condition_variable SDLVideoPlayer::m_pcmCvWrite;
double SDLVideoPlayer::m_currentPts = 0;

std::atomic_bool g_bReadAudio = false;
int g_ReadLenExpect = 0;

SDLVideoPlayer::SDLVideoPlayer()
{
	m_sdlUiThread.reset(new std::thread(&SDLVideoPlayer::ShowPlayUI, this));
	m_demuxThread.reset(new std::thread(&SDLVideoPlayer::DoDemuex, this));
	m_videoDecodeThread.reset(new std::thread(&SDLVideoPlayer::DoDecodeVideo, this));
	m_audioDecodeThread.reset(new std::thread(&SDLVideoPlayer::DoDecodeAudio, this));
    m_sdlVideoTimerThread.reset(new std::thread(&SDLVideoPlayer::StartPictureTimer, this));
    m_sdlAudioThread.reset(new std::thread(&SDLVideoPlayer::ShowPlayAudio, this));
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

	if (m_sdlVideoTimerThread && m_sdlVideoTimerThread->joinable())
	{
        m_sdlVideoTimerThread->join();
	}

    if (m_sdlAudioThread && m_sdlAudioThread->joinable())
    {
        m_sdlAudioThread->join();
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
	m_startCv.notify_all();
}

void SDLVideoPlayer::StopPlay()
{
	m_bStartPlay = false;
	m_startCv.notify_all();
	m_videoTaskCv.notify_all();
	m_audioTaskCv.notify_all();
    SDL_PauseAudio(1);

	if (m_imgConvertCtx)
	{
		sws_freeContext(m_imgConvertCtx);
		m_imgConvertCtx = nullptr;
	}

    if (m_audioConvertCtx) {
        swr_free(&m_audioConvertCtx);
        m_audioConvertCtx = nullptr;
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
		if (!m_videoDecoder.Init(&m_videoCodecParams) || !m_audioDecoder.Init(&m_audioCodecParams))
		{
			continue;
		}

		//if(!m_audio)

		int videoIndex = m_demuxer.GetVideoStreamId();
		int audioIndex = m_demuxer.GetAudioStremId();

		std::cout << "Video total seconds: " << m_demuxer.GetVideoTotalSecond() << std::endl;
		std::cout << "Audio total seconds: " << m_demuxer.GetAudioTotalSecond() << std::endl;

		SDL_Event event;
		event.type = SFM_NEW_EVENT;
		SDL_PushEvent(&event);

		lock.unlock();
        m_bDemuxFinish = false;
		while (!m_bExitFlag && m_bStartPlay)
		{
            std::unique_lock<std::mutex> lock(m_audioPcmMutex);
            if (m_audioPcmBufLen < g_ReadLenExpect) {
                lock.unlock();
                AVPacket *pkt = m_demuxer.GetPacket();
                if (!pkt)
                {
                    //m_bStartPlay = false;
                    m_bDemuxFinish = true;
                    break;
                }
                else
                {
                    if (pkt->stream_index == videoIndex)
                    {
                        std::unique_lock<std::mutex> lock(m_videoTaskMutex);
                        m_videoDecodeTask.push(pkt);
                        m_videoTaskCv.notify_one();

                        /*if (m_videoDecodeTask.size() > 5)
                        {
                            m_videoTaskCv.wait(lock);
                        }*/
                    }
                    else if (pkt->stream_index == audioIndex)
                    {
                        std::unique_lock<std::mutex> lock(m_audioTaskMutex);
                        m_audioDecodeTask.push(pkt);
                        m_audioTaskCv.notify_one();

                        /*if (m_audioDecodeTask.size() > 10)
                        {
                            m_audioTaskCv.wait(lock);
                        }*/
                    }
                    else {
                        av_packet_unref(pkt);
                        av_packet_free(&pkt);
                    }
                }
            }
            else
            {
                m_pcmCvRead.wait(lock);
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
				m_videoFrameTimestampQueue.push(frame->pts * av_q2d(m_demuxer.GetVideoTimeBase()) * 1000);

				if (m_videoFrameQueue.size() > 25)
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
            m_audioTaskCv.notify_one();

			//decode pkt
			std::cout << pkt->pos << " : audio packet" << std::endl;
            m_audioDecoder.DecodePacket(pkt);
            while (!m_bExitFlag)
            {
                std::unique_lock<std::mutex> lock(m_audioPcmMutex);
                if (m_audioPcmBufLen > g_ReadLenExpect) {
                    m_pcmCvRead.wait(lock);
                    continue;
                }
                lock.unlock();

                AVFrame *frame = m_audioDecoder.GetDecodedFrame();
                if (frame == nullptr)
                {
                    break;
                }
                //m_audioFrameTimestampQueue.push(frame->pts * av_q2d(m_demuxer.GetAudioTimeBase()) * 1000);
                

                ReadAudioFrame(frame);
                //if (m_audioFrameQueue.size() > 100)
                //{ // cache
                //    m_audioFrameCv.wait(frameLock);
                //}
            }

			av_packet_unref(pkt);
            av_packet_free(&pkt);
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

        auto ts = GetMillSecondsTimestamp();
        if (ts - m_mousetLastActiveTs > 1000) {
            SDL_ShowCursor(false);
        }
        else {
            SDL_ShowCursor(true);
        }

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
			m_timerCv.notify_all();
		}
		else if (event.type == SFM_QUIT_EVENT)
		{
			SDL_Quit();
			return;
		}
		else if (event.type == SFM_REFRESH_PIC_EVENT)
		{
			// next frame
			int width, height;
			SDL_GetWindowSize(m_sdlMainWindow, &width, &height);
			if (m_screenWidth != width || m_screenHight != height)
			{
				SDL_Event event;
				event.type = SDL_WINDOWEVENT;
				event.window.event = SDL_WINDOWEVENT_RESIZED;
				SDL_PushEvent(&event);
			}
            PlayPictureFrame();
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
		else if (event.type == SDL_KEYDOWN)
		{
			if (event.key.keysym.scancode == SDL_SCANCODE_RETURN)
			{
				//FullScreen
				if (SDL_GetWindowFlags(m_sdlMainWindow) & SDL_WINDOW_FULLSCREEN_DESKTOP)
				{
					SDL_SetWindowFullscreen(m_sdlMainWindow, 0);
					SDL_RestoreWindow(m_sdlMainWindow);
					SDL_SetWindowPosition(m_sdlMainWindow, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
				}
				else
				{
					SDL_SetWindowFullscreen(m_sdlMainWindow, SDL_WINDOW_FULLSCREEN_DESKTOP);
				}
			}
        }
        else if (event.type == SDL_MOUSEMOTION || event.type == SDL_MOUSEBUTTONDOWN) {
            m_mousetLastActiveTs = ts;
        }
	}
}

void SDLVideoPlayer::PlayPictureFrame()
{
	m_videoFrameMutex.lock();
	if (m_videoFrameQueue.empty())
	{
		m_videoFrameCv.notify_one();
		m_videoFrameMutex.unlock();

		if (m_bStartPlay && m_bDemuxFinish)
		{
            m_videoTaskMutex.lock();
            m_audioTaskMutex.lock();
            if (m_videoDecodeTask.empty() && m_audioDecodeTask.empty()) {
                //Finish
                StopPlay();
            }
            m_audioTaskMutex.unlock();
            m_videoTaskMutex.unlock();
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


void SDLVideoPlayer::ShowPlayAudio() {
    SDL_Event event;
    while (!m_bExitFlag)
    {
        std::unique_lock<std::mutex> lock(m_timerMutex);
        m_timerCv.wait(lock);
        lock.unlock();

        if (m_bExitFlag)
        {
            return;
        }

        if (!m_bStartPlay)
        {
            continue;
        }

        auto audioCtx = m_audioDecoder.GetCodecContext();
        m_sdlAudioSpec.freq = audioCtx->sample_rate;//根据你录制的PCM采样率决定
        m_sdlAudioSpec.format = AUDIO_S16SYS;
        m_sdlAudioSpec.channels = audioCtx->channels;
        m_sdlAudioSpec.silence = 0;
        m_sdlAudioSpec.samples = m_audioCodecParams.frame_size;
        m_sdlAudioSpec.callback = &SDLVideoPlayer::ReadAudioData;
        m_sdlAudioSpec.userdata = NULL;
        int re = SDL_OpenAudio(&m_sdlAudioSpec, NULL);
        if (re < 0) {
            std::cout << "can't open audio: " << GetErrorInfo(re);
        }
        else {
            //Start play audio
            SDL_PauseAudio(0);
        }
    }
}

void SDLVideoPlayer::ReadAudioFrame(AVFrame *frame) {

    if (!frame) {
        return;
    }

    if (!m_audioConvertCtx) {
        auto ctx = m_audioDecoder.GetCodecContext();
        m_audioConvertCtx = swr_alloc();
        m_audioConvertCtx = swr_alloc_set_opts(
            m_audioConvertCtx,
            av_get_default_channel_layout(2),	//输出通道数
            AV_SAMPLE_FMT_S16,					//输出格式
            ctx->sample_rate,					//输出采样率
            ctx->channel_layout,					//输入通道数
            ctx->sample_fmt,						//输入格式
            ctx->sample_rate,					//输入采样率
            0,
            0
        );
        int re = swr_init(m_audioConvertCtx);
        if (re != 0) {
            std::cout << "swr_init failed: " << GetErrorInfo(re) << std::endl;
            return;
        }
    }

    uint8_t *data[2] = { 0 };
    m_audioPcmMutex.lock();
    std::cout << "---------------------------1------------" << std::endl;
    data[0] = m_audioPcmDataBuf + m_audioPcmBufLen;
    std::cout << "---------------------------2------------" << std::endl;
    int re = swr_convert(m_audioConvertCtx,
        data,
        frame->nb_samples,
        (const uint8_t**)frame->data,
        frame->nb_samples);
    std::cout << "---------------------------3------------" << std::endl;
    m_audioPcmBufLen += frame->nb_samples * 2 * 2;
    std::cout << "---------------------------4------------" << std::endl;

    if (m_audioPcmBufLen >= g_ReadLenExpect) {
        m_currentPts = frame->pts * av_q2d(m_demuxer.GetAudioTimeBase()) * 1000;
        m_pcmCvWrite.notify_one();
    }

    m_audioPcmMutex.unlock();

    av_frame_free(&frame);
}

void SDLVideoPlayer::StartPictureTimer()
{
	while (!m_bExitFlag)
	{
		std::unique_lock<std::mutex> lock(m_timerMutex);
		m_timerCv.wait(lock);
		lock.unlock();

        double currentPts = 0;
		while (!m_bExitFlag && m_bStartPlay)
		{	
            double videoNextPts;
            //double audioNextPts;
            GetPts(StreamType::Video, currentPts, videoNextPts);

            SDL_Event event;
            std::cout << "-------------video---------------------: " << (int)(videoNextPts - currentPts) << std::endl;
            SDL_Delay((int)(videoNextPts - currentPts));
            event.type = SFM_REFRESH_PIC_EVENT;
            SDL_PushEvent(&event);
            currentPts = videoNextPts;
		}
	}
}

void SDLVideoPlayer::GetPts(StreamType type, double currentPts, double &nextPts) {
    if (type == StreamType::Video) {
        m_videoFrameMutex.lock();
    }
    else {
        m_audioFrameMutex.lock();
    }

    nextPts = 0;

    if (type == StreamType::Video) {
        if (m_videoFrameTimestampQueue.empty())
        {
            nextPts = currentPts + 40; //25fps
        }
        else
        {
            nextPts = m_videoFrameTimestampQueue.front();
            m_videoFrameTimestampQueue.pop();
        }
    }
    else {
        if (m_audioFrameTimestampQueue.empty())
        {
            nextPts = currentPts + 10; //100fps
        }
        else
        {
            nextPts = m_audioFrameTimestampQueue.front();
            m_audioFrameTimestampQueue.pop();
        }
    }

    if (type == StreamType::Video) {

        //// 同步音视频，根据当前音频的时间戳调整视频帧的播放速度
        //if (nextPts < m_currentPts - 5 || nextPts > m_currentPts - 5) {
        //    nextPts = m_currentPts;
        //}

        if (nextPts <= currentPts) {
            nextPts = currentPts + 5;
        }

        m_videoFrameMutex.unlock();
    }
    else {
        if (nextPts < currentPts) {
            nextPts = currentPts + 10;
        }
        m_audioFrameMutex.unlock();
    }
}

char* SDLVideoPlayer::GetErrorInfo(const int code) {
    memset(m_buf, 0, sizeof(m_buf));
    av_strerror(code, m_buf, sizeof(m_buf));
    return m_buf;
}


void SDLVideoPlayer::ReadAudioData(void *udata, Uint8 *stream, int len) {
    SDL_memset(stream, 0, len);

    std::unique_lock<std::mutex> lock(m_audioPcmMutex);

    //向设备发送长度为len的数据
    if(m_audioPcmBufLen < len) {
        g_ReadLenExpect = len;
        m_pcmCvRead.notify_all();
        m_pcmCvWrite.wait(lock);
    }

    std::cout << "len: " << len << "-------- m_audioPcmBufLen: " << m_audioPcmBufLen << std::endl;

    SDL_MixAudio(stream, m_audioPcmDataBuf, len, SDL_MIX_MAXVOLUME);
    m_audioPcmBufLen -= len;

    if (m_audioPcmBufLen > 0) {
        memcpy(m_audioPcmDataBuf, m_audioPcmDataBuf + len, m_audioPcmBufLen);
    }
}