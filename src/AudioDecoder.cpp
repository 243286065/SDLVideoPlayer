#include "AudioDecoder.h"

#include <iostream>

extern "C" {
#include "libavcodec/avcodec.h"
}

AudioDecoder::AudioDecoder()
{
}

AudioDecoder::~AudioDecoder()
{
}

bool AudioDecoder::Init(const AVCodecParameters * codecParams)
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

void AudioDecoder::Free()
{
}

bool AudioDecoder::DecodePacket(const AVPacket * pkt)
{
    return false;
}

AVFrame * AudioDecoder::GetDecodedFrame()
{
    return nullptr;
}

char * AudioDecoder::GetErrorInfo(const int code)
{
    return nullptr;
}

