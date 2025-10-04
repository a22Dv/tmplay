#include <filesystem>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavcodec/packet.h>
#include <libavfilter/avfilter.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/channel_layout.h>
#include <libavutil/frame.h>
#include <libavutil/samplefmt.h>
}
#include "decoder.hpp"
#include "utils.hpp"

namespace trm {

Decoder::Decoder(const std::filesystem::path path) {
    AVFormatContext *fctx{};
    require(std::filesystem::exists(path), Error::DOES_NOT_EXIST);
    require(avformat_open_input(&fctx, asU8(path).data(), nullptr, nullptr) >= 0, Error::FFMPEG_OPEN);
    require(avformat_find_stream_info(fctx, nullptr) >= 0, Error::FFMPEG_OPEN);
    state.aStreamIdx = av_find_best_stream(fctx, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
    require(state.aStreamIdx >= 0, Error::FFMPEG_OPEN);
    state.stream = fctx->streams[state.aStreamIdx];

    const AVCodec *codec{avcodec_find_decoder(state.stream->codecpar->codec_id)};
    require(codec, Error::FFMPEG_OPEN);
    state.codecCtx.reset(avcodec_alloc_context3(codec));
    require(state.codecCtx.get(), Error::FFMPEG_OPEN);
    require(avcodec_parameters_to_context(state.codecCtx.get(), state.stream->codecpar) >= 0, Error::FFMPEG_OPEN);
    require(avcodec_open2(state.codecCtx.get(), codec, nullptr) >= 0, Error::FFMPEG_OPEN);
    state.frame.reset(av_frame_alloc());
    state.filterFrame.reset(av_frame_alloc());
    state.packet.reset(av_packet_alloc());
    require(state.frame.get(), Error::FFMPEG_OPEN);
    require(state.filterFrame.get(), Error::FFMPEG_OPEN);
    require(state.packet.get(), Error::FFMPEG_OPEN);
    state.formatCtx.reset(fctx);
    setFilterGraph();
    acquireFFrame();
    state.validState = true;
}

void Decoder::setFilterGraph() {
    state.validState = false;
    state.filterInCtx = nullptr;
    state.filterOutCtx = nullptr;

    AVFilterInOut *in{avfilter_inout_alloc()};
    AVFilterInOut *out{avfilter_inout_alloc()};
    try {
        state.filterGraph.reset(avfilter_graph_alloc());
        require(in && out && state.filterGraph, Error::ALLOC);
        if (state.codecCtx->ch_layout.order == AV_CHANNEL_ORDER_UNSPEC) {
            av_channel_layout_default(&state.codecCtx->ch_layout, state.codecCtx->ch_layout.nb_channels);
        }
        char layoutDesc[128];
        av_channel_layout_describe(&state.codecCtx->ch_layout, layoutDesc, 128);
        std::string args{std::format(
            "time_base={}/{}:sample_rate={}:sample_fmt={}:channel_layout={}", state.stream->time_base.num,
            state.stream->time_base.den, state.codecCtx->sample_rate,
            av_get_sample_fmt_name(state.codecCtx->sample_fmt), layoutDesc
        )};
        require(
            avfilter_graph_create_filter(
                &state.filterInCtx, avfilter_get_by_name("abuffer"), "in", args.c_str(), nullptr,
                state.filterGraph.get()
            ) >= 0,
            Error::FFMPEG_FILTER
        );
        require(
            avfilter_graph_create_filter(
                &state.filterOutCtx, avfilter_get_by_name("abuffersink"), "out", nullptr, nullptr,
                state.filterGraph.get()
            ) >= 0,
            Error::FFMPEG_FILTER
        );
        in->name = av_strdup("out");
        in->filter_ctx = state.filterOutCtx;
        in->pad_idx = 0;
        in->next = nullptr;
        out->name = av_strdup("in");
        out->filter_ctx = state.filterInCtx;
        out->pad_idx = 0;
        out->next = nullptr;
        require(
            avfilter_graph_parse_ptr(state.filterGraph.get(), state.filterDesc, &in, &out, nullptr) >= 0,
            Error::FFMPEG_FILTER
        );
        avfilter_inout_free(&in);
        avfilter_inout_free(&out);
        require(avfilter_graph_config(state.filterGraph.get(), nullptr) >= 0, Error::FFMPEG_FILTER);
        state.validState = true;
    } catch (...) {
        avfilter_inout_free(&in);
        avfilter_inout_free(&out);
        throw;
    }
}

} // namespace trm