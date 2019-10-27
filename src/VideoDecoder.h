#pragma once

enum AVCodecID;
struct AVCodec;
struct AVCodecContext;
struct AVCodecParameters;
struct AVFrame;
struct AVPacket;

class VideoDecoder {
public:
    VideoDecoder();
    ~VideoDecoder();

    bool Init(const AVCodecParameters* codecParams);
    void Free();

    int GetVideoWidth();
    int GetVideoHeight();

    bool DecodePacket(const AVPacket* pkt);
    AVFrame* GetDecodedFrame();
private:
    char* GetErrorInfo(const int code);
    AVCodec *m_codec = nullptr;
    AVCodecContext *m_ctx = nullptr;

    char m_buf[1024] = { 0 };
};