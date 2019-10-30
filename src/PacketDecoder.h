#pragma once

enum AVCodecID;
struct AVCodec;
struct AVCodecContext;
struct AVCodecParameters;
struct AVFrame;
struct AVPacket;

class PacketDecoder {
public:
    PacketDecoder();
    ~PacketDecoder();

    bool Init(const AVCodecParameters* codecParams);
    void Free();

    int GetVideoWidth();
    int GetVideoHeight();
    AVCodecContext* GetCodecContext() { return m_ctx; }

    bool DecodePacket(const AVPacket* pkt);
    AVFrame* GetDecodedFrame();
private:
    char* GetErrorInfo(const int code);
    AVCodec *m_codec = nullptr;
    AVCodecContext *m_ctx = nullptr;

    char m_buf[1024] = { 0 };
};