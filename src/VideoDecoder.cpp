#include "VideoDecoder.h"
#include <iostream>

extern "C" {
#include "libavcodec/avcodec.h"
}

VideoDecoder::VideoDecoder()
{
}

VideoDecoder::~VideoDecoder()
{
}

bool VideoDecoder::Init(const AVCodecParameters* codecParams)
{
    if (!codecParams) {
        return false;
    }

    m_codec = avcodec_find_decoder(codecParams->codec_id);
    if (m_codec == NULL) {
        std::cout << "Cannot find decoder with id: " << codecParams->codec_id << std::endl;
        return false;
    }

    std::cout << "Find decoder with id: " << codecParams->codec_id << std::endl;

    m_ctx = avcodec_alloc_context3(m_codec);
    if (m_ctx == NULL) {
        std::cout << "Cannot alloc decoder with id: " << codecParams->codec_id << std::endl;
        return false;
    }
    std::cout << "Alloc decoder with id: " << codecParams->codec_id << std::endl;

    avcodec_parameters_to_context(m_ctx, codecParams);

    if (avcodec_open2(m_ctx, NULL, NULL) < 0) {
        std::cout << "Failed to open decoder with id: " << codecParams->codec_id << std::endl;
        Free();
        return false;
    }

    return true;
}

void VideoDecoder::Free() {
    if (m_ctx) {
        avcodec_close(m_ctx);
        avcodec_free_context(&m_ctx);
        m_ctx = nullptr;
    }
}

int VideoDecoder::GetVideoWidth() {
    if (m_ctx) {
        return m_ctx->width;
    }

    return 0;
}

int VideoDecoder::GetVideoHeight() {
    if (m_ctx) {
        return m_ctx->height;
    }

    return 0;
}

bool VideoDecoder::DecodePacket(const AVPacket* pkt) {
    //Send packet to decode thread;
    int re = avcodec_send_packet(m_ctx, pkt);
    if (re != 0) {
        std::cout << "avcodec_send_packet failed: " << GetErrorInfo(re) << std::endl;
        return false;
    }

    return true;
}

AVFrame* VideoDecoder::GetDecodedFrame() {
    AVFrame *frame = av_frame_alloc();
    int re = avcodec_receive_frame(m_ctx, frame);
    if (re != 0) {
        std::cout << "avcodec_send_packet failed: " << GetErrorInfo(re) << std::endl;
        av_frame_free(&frame);
        return nullptr;;
    }

    std::cout << "Get Frame"  << std::endl;
    return frame;
}

char* VideoDecoder::GetErrorInfo(const int code) {
    memset(m_buf, 0, sizeof(m_buf));
    av_strerror(code, m_buf, sizeof(m_buf));
    return m_buf;
}