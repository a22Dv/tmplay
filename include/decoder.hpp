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
    int cSample{0};
    bool eof{};
    bool fGraphEof{};
    bool fEof{};
    bool pEof{};
    bool validState{};
    std::unique_ptr<AVFrame, decltype([](AVFrame *f) { av_frame_free(&f); })> frame{};
    std::unique_ptr<AVFrame, decltype([](AVFrame *f) { av_frame_free(&f); })> filterFrame{};
    std::unique_ptr<AVCodecContext, decltype([](AVCodecContext *f) { avcodec_free_context(&f); })> codecCtx{};
    std::unique_ptr<AVFormatContext, decltype([](AVFormatContext *f) { avformat_close_input(&f); })> formatCtx{};
    std::unique_ptr<AVFilterGraph, decltype([](AVFilterGraph *f) { avfilter_graph_free(&f); })> filterGraph{};
    std::unique_ptr<AVPacket, decltype([](AVPacket *f) { av_packet_free(&f); })> packet{};
    AVStream *stream{};
    AVFilterContext *filterInCtx{};
    AVFilterContext *filterOutCtx{};
    static constexpr const char *filterDesc{"aresample=48000,aformat=sample_fmts=s16:channel_layouts=stereo"};
};

enum class DecodeStatus : std::uint8_t {
    AV_SUCCESS,
    AV_AGAIN,
    AV_EXCEPTION,
    AV_EOF,
};

// Not a thread-safe implementation. Must only interact with 1 thread.
class Decoder {
    FileData data{};
    DecodeState state{};
    std::optional<std::int16_t>  acquireSample();
    DecodeStatus retrFFrame() noexcept;
    DecodeStatus acquireFFrame() noexcept;
    DecodeStatus retrFrame() noexcept;
    DecodeStatus acquireFrame() noexcept;
    DecodeStatus retrPacket() noexcept;
    DecodeStatus acquirePacket() noexcept;
    void setFilterGraph();

  public:
    bool isReady() { return state.validState; };
    bool eof() { return state.eof; }
    float getFileDuration() { return data.duration; }
    float getCurrentTimestamp() { return data.timestamp; }
    void seekTo(const float timestamp);
    std::optional<std::int16_t> getSample();
    Decoder() {};
    Decoder(const std::filesystem::path path);
};

} // namespace trm
