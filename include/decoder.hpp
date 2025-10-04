#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavfilter/avfilter.h>
#include <libavfilter/buffersink.h>
#include <libavfilter/buffersrc.h>
#include <libavformat/avformat.h>
}

namespace trm {

struct FileData {
    float duration{};
    float timestamp{};
    std::filesystem::path path{};
};

struct DecodeState {
    int aStreamIdx{-1};
    bool eof{};
    bool fGraphEof{};
    bool fEof{};
    bool pEof{};
    bool validState{};
    std::unique_ptr<AVFrame, decltype([](AVFrame *f) { av_frame_free(&f); })> frame{};
    std::unique_ptr<AVFrame, decltype([](AVFrame *f) { av_frame_free(&f); })> filterFrame{};
    std::unique_ptr<AVCodecContext, decltype([](AVCodecContext *f) { avcodec_free_context(&f); })> codecCtx{};
    std::unique_ptr<AVFormatContext, decltype([](AVFormatContext *f) { avformat_free_context(f); })> formatCtx{};
    std::unique_ptr<AVFilterGraph, decltype([](AVFilterGraph *f) { avfilter_graph_free(&f); })> filterGraph{};
    std::unique_ptr<AVPacket, decltype([](AVPacket *f) { av_packet_free(&f); })> packet{};
    AVStream *stream{};
    AVFilterContext *filterInCtx{};
    AVFilterContext *filterOutCtx{};
    static constexpr const char *filterDesc{"aresample=48000,aformat=sample_fmts=s16:channel_layouts=stereo"};
};

class Decoder {
    FileData data{};
    DecodeState state{};
    void retrSample();
    void acquireSample();
    void retrFFrame();
    void acquireFFrame();
    void retrFrame();
    void acquireFrame();
    void retrPacket();
    void acquirePacket();
    void setFilterGraph();

  public:
    bool isReady();
    bool eof() { return state.eof; }
    float getFileDuration() { return data.duration; }
    float getCurrentTimestamp() { return data.timestamp; }
    float seekTo(const float timestamp);
    std::int16_t getSample();
    Decoder();
    Decoder(const std::filesystem::path path);
};

} // namespace trm
