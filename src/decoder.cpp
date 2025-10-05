#include <cstdint>
#include <filesystem>
#include <format>
#include <optional>
#include <string>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavcodec/packet.h>
#include <libavfilter/avfilter.h>
#include <libavfilter/buffersink.h>
#include <libavfilter/buffersrc.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/channel_layout.h>
#include <libavutil/frame.h>
#include <libavutil/samplefmt.h>
}

#include "decoder.hpp"
#include "maudio.hpp"
#include "utils.hpp"

namespace trm {

namespace {

std::int64_t toStreamTicks(const float from, const AVRational streamUnits) {
    return av_rescale_q(static_cast<std::int64_t>(from * AV_TIME_BASE), AV_TIME_BASE_Q, streamUnits);
}

float fromStreamTicks(const std::int64_t from, const AVRational streamUnits) {
    return static_cast<float>(av_rescale_q(from, streamUnits, AV_TIME_BASE_Q)) / AV_TIME_BASE;
}

} // namespace

Decoder::Decoder(const std::filesystem::path path) {
    AVFormatContext *fctx{};
    require(std::filesystem::exists(path), Error::DOES_NOT_EXIST);
    require(avformat_open_input(&fctx, asU8(path).data(), nullptr, nullptr) >= 0, Error::FFMPEG_OPEN);
    require(avformat_find_stream_info(fctx, nullptr) >= 0, Error::FFMPEG_OPEN);
    state.aStreamIdx = av_find_best_stream(fctx, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
    require(state.aStreamIdx >= 0, Error::FFMPEG_OPEN);
    state.stream = fctx->streams[state.aStreamIdx];
    data.duration = static_cast<float>(fctx->duration) / AV_TIME_BASE;
    data.path = path;
    data.timestamp = 0.0f;

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
        std::string inArgs{std::format(
            "time_base={}/{}:sample_rate={}:sample_fmt={}:channel_layout={}", state.stream->time_base.num,
            state.stream->time_base.den, state.codecCtx->sample_rate,
            av_get_sample_fmt_name(state.codecCtx->sample_fmt), layoutDesc
        )};
        require(
            avfilter_graph_create_filter(
                &state.filterInCtx, avfilter_get_by_name("abuffer"), "in", inArgs.c_str(), nullptr,
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

std::optional<std::int16_t> Decoder::getSample() { return acquireSample(); }

std::optional<std::int16_t> Decoder::acquireSample() {
    constexpr float denum{MaDeviceSpecifiers::channels * MaDeviceSpecifiers::sampleRate};
    const AVRational timeBase{av_buffersink_get_time_base(state.filterOutCtx)};
    while (!state.eof) {
        if (state.cSample < static_cast<int>(state.filterFrame->nb_samples * MaDeviceSpecifiers::channels)) [[likely]] {
            data.timestamp = fromStreamTicks(state.filterFrame->pts, timeBase) + state.cSample / denum;
            return reinterpret_cast<std::int16_t *>(state.filterFrame->data[0])[state.cSample++];
        }
        const DecodeStatus fAcq{acquireFFrame()};
        if (fAcq == DecodeStatus::AV_EOF) [[unlikely]] {
            state.eof = true;
            break;
        }
        require(fAcq != DecodeStatus::AV_EXCEPTION, Error::FFMPEG_DECODE);
        state.cSample = 0;
    }
    return std::nullopt;
}

DecodeStatus Decoder::retrFFrame() noexcept {
    av_frame_unref(state.filterFrame.get());
    const int out{av_buffersink_get_frame(state.filterOutCtx, state.filterFrame.get())};
    if (out == AVERROR(EAGAIN)) {
        return DecodeStatus::AV_AGAIN;
    } else if (out == AVERROR_EOF) {
        return DecodeStatus::AV_EOF;
    } else if (out < 0) [[unlikely]] {
        return DecodeStatus::AV_EXCEPTION;
    }
    return DecodeStatus::AV_SUCCESS;
}

DecodeStatus Decoder::acquireFFrame() noexcept {
    while (!state.fGraphEof) {
        DecodeStatus retr{retrFFrame()};
        if (retr == DecodeStatus::AV_AGAIN) {
            const DecodeStatus fAcq{acquireFrame()};
            if (fAcq == DecodeStatus::AV_SUCCESS) {
                const int srcAdd{av_buffersrc_add_frame(state.filterInCtx, state.frame.get())};
                if (srcAdd < 0) [[unlikely]] {
                    return DecodeStatus::AV_EXCEPTION;
                }
                continue;
            } else if (fAcq == DecodeStatus::AV_EXCEPTION) [[unlikely]] {
                return fAcq;
            }
            const int srcAdd{av_buffersrc_add_frame(state.filterInCtx, nullptr)};
            if (srcAdd < 0) [[unlikely]] {
                return DecodeStatus::AV_EXCEPTION;
            }
            continue;
        } else if (retr == DecodeStatus::AV_EOF) {
            state.fGraphEof = true;
            return DecodeStatus::AV_EOF;
        } else if (retr == DecodeStatus::AV_EXCEPTION) [[unlikely]] {
            return retr;
        }
        return DecodeStatus::AV_SUCCESS;
    }
    return DecodeStatus::AV_EOF;
}

DecodeStatus Decoder::retrFrame() noexcept {
    av_frame_unref(state.frame.get());
    const int rec{avcodec_receive_frame(state.codecCtx.get(), state.frame.get())};
    if (rec == AVERROR(EAGAIN)) {
        return DecodeStatus::AV_AGAIN;
    } else if (rec == AVERROR_EOF) {
        return DecodeStatus::AV_EOF;
    } else if (rec < 0) [[unlikely]] {
        return DecodeStatus::AV_EXCEPTION;
    }
    return DecodeStatus::AV_SUCCESS;
}

DecodeStatus Decoder::acquireFrame() noexcept {
    while (!state.fEof) {
        const DecodeStatus retr{retrFrame()};
        if (retr == DecodeStatus::AV_AGAIN) {
            const DecodeStatus acqPkt{acquirePacket()};
            if (acqPkt == DecodeStatus::AV_EOF) {
                int sendPkt{avcodec_send_packet(state.codecCtx.get(), nullptr)};
                if (sendPkt < 0) [[unlikely]] {
                    return DecodeStatus::AV_EXCEPTION;
                }
                continue;
            } else if (acqPkt == DecodeStatus::AV_EXCEPTION) [[unlikely]] {
                return acqPkt;
            }
            const int sendPkt{avcodec_send_packet(state.codecCtx.get(), state.packet.get())};
            if (sendPkt < 0) [[unlikely]] {
                return DecodeStatus::AV_EXCEPTION;
            }
            continue;
        } else if (retr == DecodeStatus::AV_EOF) {
            state.fEof = true;
            return retr;
        } else if (retr == DecodeStatus::AV_EXCEPTION) [[unlikely]] {
            return retr;
        }
        return DecodeStatus::AV_SUCCESS;
    }
    return DecodeStatus::AV_EOF;
}

DecodeStatus Decoder::retrPacket() noexcept {
    av_packet_unref(state.packet.get());
    const int read{av_read_frame(state.formatCtx.get(), state.packet.get())};
    if (read == AVERROR_EOF) {
        state.pEof = true;
        return DecodeStatus::AV_EOF;
    } else if (read < 0) [[unlikely]] {
        return DecodeStatus::AV_EXCEPTION;
    }
    return DecodeStatus::AV_SUCCESS;
}

DecodeStatus Decoder::acquirePacket() noexcept {
    while (!state.pEof) {
        const DecodeStatus retr{retrPacket()};
        if (retr != DecodeStatus::AV_SUCCESS) {
            return retr;
        }
        if (state.packet->stream_index == state.aStreamIdx) {
            return DecodeStatus::AV_SUCCESS;
        }
    }
    return DecodeStatus::AV_EOF;
}

void Decoder::seekTo(const float timestamp) {
    const float nTimestamp{std::max(0.0f, std::min(timestamp, data.duration))};
    const std::int64_t nTSConverted{toStreamTicks(nTimestamp, state.stream->time_base)};
    require(
        av_seek_frame(state.formatCtx.get(), state.aStreamIdx, nTSConverted, AVSEEK_FLAG_BACKWARD) >= 0,
        Error::FFMPEG_DECODE
    );
    avcodec_flush_buffers(state.codecCtx.get());
    setFilterGraph();
    state.cSample = 0;
    state.eof = state.fGraphEof = state.fEof = state.pEof = false;
    while (state.filterFrame->pts < nTSConverted) {
        if (acquireFFrame() != DecodeStatus::AV_SUCCESS) {
            break;
        }
    }
    data.timestamp = nTimestamp;
}

} // namespace trm