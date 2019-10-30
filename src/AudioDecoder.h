#pragma once

struct AVCodecParameters;
struct AVPacket;
struct AVFrame;
struct AVCodec;
struct AVCodecContext;
struct SwsContext;

class AudioDecoder {
public:
    AudioDecoder();
    ~AudioDecoder();

    bool Init(const AVCodecParameters* codecParams);
    void Free();

    bool DecodePacket(const AVPacket* pkt);
    AVFrame* GetDecodedFrame();

private:
    char* GetErrorInfo(const int code);
    AVCodec *m_codec = nullptr;
    AVCodecContext *m_ctx = nullptr;

    char m_buf[1024] = { 0 };
};