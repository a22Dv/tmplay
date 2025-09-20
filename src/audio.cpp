#define MINIAUDIO_IMPLEMENTATION
#include <miniaudio.h>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavcodec/codec.h>
#include <libavcodec/packet.h>
#include <libavfilter/avfilter.h>
#include <libavfilter/buffersink.h>
#include <libavfilter/buffersrc.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/channel_layout.h>
#include <libavutil/frame.h>
#include <libavutil/log.h>
#include <libavutil/mathematics.h>
#include <libavutil/mem.h>
#include <libavutil/opt.h>
#include <libavutil/samplefmt.h>
}

#include <algorithm>
#include <array>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>

#include "player.hpp"
#include "utils.hpp"

namespace tml {

/// Constructor.
Audio::~Audio() {
    state.terminate.store(true);
    state.conVar.notify_one();
    if (producerThread.joinable()) {
        producerThread.join();
    }
}

namespace detail {

using AVFormatContextUP = std::unique_ptr<AVFormatContext, deleter<AVFormatContext, avformat_close_input>>;
using AVCodecContextUP = std::unique_ptr<AVCodecContext, deleter<AVCodecContext, avcodec_free_context>>;
using AVFilterGraphUP = std::unique_ptr<AVFilterGraph, deleter<AVFilterGraph, avfilter_graph_free>>;
using AVPacketUP = std::unique_ptr<AVPacket, deleter<AVPacket, av_packet_free>>;
using AVFrameUP = std::unique_ptr<AVFrame, deleter<AVFrame, av_frame_free>>;
using AVFilterInOutUP = std::unique_ptr<AVFilterInOut, deleter<AVFilterInOut, avfilter_inout_free>>;
using AVStreamOP = observer_ptr<AVStream>;
using AVFilterContextOP = observer_ptr<AVFilterContext>;

enum class DecoderStatus : std::uint8_t { SUCCESS, END_OF_FILE, EXCEPTION, AGAIN, END_OF_BUFFER };

/// Audio decoder using FFmpeg underneath.
class Decoder {
    int aStreamIdx{-1};
    bool filterGraphEof{};
    bool frameEof{};
    bool packetEof{};
    bool isValid{};
    float duration{};
    fs::path filepath{};
    std::size_t cSampleIdx{};
    std::chrono::duration<float> timestamp{};
    AVFormatContextUP formatContext{};
    AVCodecContextUP codecContext{};
    AVFilterContextOP bSinkContext{};
    AVFilterContextOP bSrcContext{};
    AVFilterGraphUP filterGraph{};
    AVStreamOP audioStream{};
    AVPacketUP packet{};
    AVFrameUP frame{};
    AVFrameUP filteredFrame{};
    static constexpr const char *filterDescription{"aresample=48000,aformat=sample_fmts=s16:channel_layouts=stereo"};

    /// Sets up the current Decoder's codecContext.
    void openCodecContext() {
        /*
            Find the most suitable audio stream within formatContext,
            Set stream to given ID, find a decoder for that stream's codec.
            Allocate for context, and transfer the stream's codec parameters
            to the context, then open it.
        */
        int aStreamIdxL{};
        AVStream *stream{};
        const AVCodec *codec{};
        int ret{av_find_best_stream(formatContext.get(), AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0)};
        require(ret >= 0, Error::FFMPEG_STREAM);
        aStreamIdxL = ret;
        stream = formatContext->streams[aStreamIdxL];
        codec = avcodec_find_decoder(stream->codecpar->codec_id);
        require(codec, Error::FFMPEG_DECODER);
        codecContext.reset(avcodec_alloc_context3(codec));
        require(codecContext.get(), Error::FFMPEG_CONTEXT);
        ret = avcodec_parameters_to_context(codecContext.get(), stream->codecpar);
        require(ret >= 0, Error::FFMPEG_CONTEXT);
        ret = avcodec_open2(codecContext.get(), codec, nullptr);
        require(ret >= 0, Error::FFMPEG_DECODER);
        aStreamIdx = aStreamIdxL;
        audioStream.reset(stream);
        if (formatContext->duration != AV_NOPTS_VALUE) {
            duration = fromStreamTicks(formatContext->duration).count();
        } else {
            duration = 0.0f;
        }
    }

    /// Initializes the current Decoder's filterGraph.
    void initFilterGraph() {
        /*
            Initializes the filter graph pipeline.
            This is responsible for turning audio data into an
            expected format (e.g. 48kHz, Stereo).
        */
        std::string arguments{};
        const int sampleRate{Audio::sampleRate};
        AVRational timeBase{audioStream->time_base};
        AVFilterInOutUP in{avfilter_inout_alloc()};
        AVFilterInOutUP out{avfilter_inout_alloc()};

        bSrcContext.reset();
        bSinkContext.reset();
        filterGraph.reset(avfilter_graph_alloc());
        require(out && in && filterGraph, Error::FFMPEG_ALLOC);

        // Set default if unspecified.
        if (codecContext->ch_layout.order == AV_CHANNEL_ORDER_UNSPEC) {
            av_channel_layout_default(&codecContext->ch_layout, codecContext->ch_layout.nb_channels);
        }
        // Assemble arguments, then assemble audio buffer source and sink.
        char description[cStyleBufferLimit];
        av_channel_layout_describe(&codecContext->ch_layout, description, cStyleBufferLimit);
        arguments = std::format(
            "time_base={}/{}:sample_rate={}:sample_fmt={}:channel_layout={}", timeBase.num, timeBase.den,
            codecContext->sample_rate, av_get_sample_fmt_name(codecContext->sample_fmt), description
        );

        int ret{avfilter_graph_create_filter(
            bSrcContext.getAddress(), avfilter_get_by_name("abuffer"), "in", arguments.c_str(), nullptr,
            filterGraph.get()
        )};
        require(ret >= 0, Error::FFMPEG_FILTER);

        ret = avfilter_graph_create_filter(
            bSinkContext.getAddress(), avfilter_get_by_name("abuffersink"), "out", nullptr, nullptr, filterGraph.get()
        );
        require(ret >= 0, Error::FFMPEG_FILTER);

        // Swapped due to the fact these specify connections.
        // Not something within the filter itself. inout_free takes care of av_strdup.
        in->name = av_strdup("out");
        in->filter_ctx = bSinkContext.get();
        in->pad_idx = 0;
        in->next = nullptr;
        out->name = av_strdup("in");
        out->filter_ctx = bSrcContext.get();
        out->pad_idx = 0;
        out->next = nullptr;

        AVFilterInOut *inR{in.get()};
        AVFilterInOut *outR{out.get()};
        require(
            avfilter_graph_parse_ptr(filterGraph.get(), filterDescription, &inR, &outR, nullptr) >= 0,
            Error::FFMPEG_FILTER
        );
        std::ignore = in.release();
        std::ignore = out.release();
        require(avfilter_graph_config(filterGraph.get(), nullptr) >= 0, Error::FFMPEG_FILTER);
    }
    /*
        AVRational is a fraction, which is why despite the formula being implemented as a * (b / c),
        where b is the destination unit, and c is the source unit, because these are fractions,
        we have to reverse them. In that a * ((1 / c) / (1 / b)) = a * (b / c). This is a simplification
        however, but that's the gist of it.
    */

    /// Converts the stream's ticks in AVStream.time_base to floating-point seconds.
    std::chrono::duration<float> fromStreamTicks(const int ticks) {
        // Convert from AVStream.time_base ticks to AV_TIME_BASE ticks. Then convert back to seconds.
        std::int64_t timeBase{av_rescale_q(ticks, audioStream->time_base, AVRational{1, AV_TIME_BASE})};
        return std::chrono::duration<float>{static_cast<float>(timeBase) / AV_TIME_BASE};
    }

    /// Converts from floating-point seconds to the current stream's AVStream.time_base.
    std::int64_t toStreamTicks(const std::chrono::duration<float> time) {
        // Convert from seconds to AV_TIME_BASE ticks. Then from that to AVStream.time_base ticks.
        return av_rescale_q(
            static_cast<std::int64_t>(time.count()) * AV_TIME_BASE, AVRational{1, AV_TIME_BASE}, audioStream->time_base
        );
    }
    /*
        The difference between retrieve* and getNew* functions
        are that the retrieve* functions are "dumb". They will
        only take whatever can be taken and fail if it can't.
        getNew always guarantees it can supply through
        the pipeline until the end.
    */

    /// Retrieves a frame from the current decoder.
    DecoderStatus retrieveFrame() noexcept {
        int ret{};
        if ((ret = avcodec_receive_frame(codecContext.get(), frame.get())) < 0) {
            if (ret == AVERROR(EAGAIN)) {
                return DecoderStatus::AGAIN;
            }
            if (ret == AVERROR_EOF) {
                return DecoderStatus::END_OF_FILE;
            }
            return DecoderStatus::EXCEPTION;
        }
        return DecoderStatus::SUCCESS;
    }

    /// Retrives a processed frame from the current filter graph.
    DecoderStatus retrieveFFrame() noexcept {
        int ret{};
        if ((ret = av_buffersink_get_frame(bSinkContext.get(), filteredFrame.get())) < 0) {
            if (ret == AVERROR(EAGAIN)) {
                return DecoderStatus::AGAIN;
            }
            if (ret == AVERROR_EOF) {
                return DecoderStatus::END_OF_FILE;
            }
            return DecoderStatus::EXCEPTION;
        }
        return DecoderStatus::SUCCESS;
    }

    /// Gets a new frame from the current decoder.
    DecoderStatus getNewFrame() noexcept {
        int ffRet{};
        DecoderStatus ret{};
        av_frame_unref(frame.get());
        while (true) {
            if ((ret = retrieveFrame()) != DecoderStatus::AGAIN) {
                if (ret == DecoderStatus::END_OF_FILE) {
                    frameEof = true;
                }
                return ret;
            }
            // Only reached when we need to send a packet.
            av_packet_unref(packet.get());
            if ((ffRet = av_read_frame(formatContext.get(), packet.get())) < 0) {
                if (ffRet == AVERROR_EOF) {
                    packetEof = true;

                    // Signal decoder for EOF.
                    avcodec_send_packet(codecContext.get(), nullptr);
                    continue;
                }
                return DecoderStatus::EXCEPTION;
            }
            // Discard packets that don't belong to our set stream.
            if (packet->stream_index != aStreamIdx) {
                continue;
            }
            avcodec_send_packet(codecContext.get(), packet.get());
        }
    }

    /// Gets a new frame from the current filter graph.
    DecoderStatus getNewFFrame() noexcept {
        int ffret{};
        DecoderStatus ret{};
        av_frame_unref(filteredFrame.get());
        while (true) {
            if ((ret = retrieveFFrame()) != DecoderStatus::AGAIN) {
                if (ret == DecoderStatus::END_OF_FILE) {
                    filterGraphEof = true;
                }
                return ret;
            }
            if ((ret = getNewFrame()) != DecoderStatus::SUCCESS) {
                if (ret == DecoderStatus::END_OF_FILE) {
                    if (av_buffersrc_add_frame(bSrcContext.get(), nullptr) < 0) {
                        return DecoderStatus::EXCEPTION;
                    }
                    continue;
                }
                return ret;
            }
            if (av_buffersrc_add_frame(bSrcContext.get(), frame.get()) < 0) {
                return DecoderStatus::EXCEPTION;
            }
        }
    }

    /// Gets a new sample from the current AVFrame. Gets a new frame if needed.
    DecoderStatus getNewSample(std::int16_t &sample) {
        DecoderStatus ret{};
        std::size_t sampleCount{filteredFrame->nb_samples * Audio::channels};
        std::int16_t *data{reinterpret_cast<std::int16_t *>(filteredFrame->data[0])};
        if (cSampleIdx < sampleCount) {
            sample = data[cSampleIdx++];
            return DecoderStatus::SUCCESS;
        }
        cSampleIdx = 0;
        if ((ret = getNewFFrame()) != DecoderStatus::SUCCESS) {
            sample = 0;
            return ret;
        }
        sampleCount = filteredFrame->nb_samples * Audio::channels;
        data = reinterpret_cast<std::int16_t *>(filteredFrame->data[0]);
        if (sampleCount == 0) {
            sample = 0;
            return DecoderStatus::END_OF_FILE;
        }
        sample = data[cSampleIdx++];
        return DecoderStatus::SUCCESS;
    };

  public:
    /// True if Decoder is in a valid state.
    bool isDecoderValid() { return isValid; };

    float getDuration() { return duration; };

    /// Returns the path of the file the current stream being executed belongs to.
    const fs::path &getPath() const { return filepath; };

    /// Decodes starting from a specific timesstamp.
    void decodeAt(std::chrono::duration<float> trgtTime) {
        const std::int64_t targetTicks{toStreamTicks(trgtTime)};
        av_seek_frame(formatContext.get(), aStreamIdx, targetTicks, 0);
        avcodec_flush_buffers(codecContext.get());
        while ((av_frame_unref(filteredFrame.get()), true) && retrieveFFrame() == DecoderStatus::SUCCESS) {
            continue;
        };
        filterGraphEof = frameEof = packetEof = false;
        initFilterGraph();
        do {
            if (getNewFFrame() != DecoderStatus::SUCCESS) {
                break;
            }
        } while (filteredFrame->pts < targetTicks);
    };

    /// Operator overload to extract one sample from the current frame.
    Decoder &operator>>(int16_t &sample) {
        DecoderStatus status{getNewSample(sample)};
        require(status != DecoderStatus::EXCEPTION, Error::FFMPEG_DECODER);
        if (status != DecoderStatus::SUCCESS) {
            sample = 0;
        }
        return *this;
    }
    explicit operator bool() const { return packetEof && frameEof && filterGraphEof; }

    /// True if the current stream being run has reached EOF.
    bool eof() const { return packetEof && frameEof && filterGraphEof; };

    /// Default constructor.
    Decoder() {};

    /// Construct decoder from audio file.
    Decoder(const fs::path &path)
        : filepath{path}, frame{av_frame_alloc()}, filteredFrame{av_frame_alloc()}, packet{av_packet_alloc()} {
        /*
            Open input, setup format context.
            Find stream, setup codec context, allocate for frames,
            setup filter graph.
        */
        AVFormatContext *fmtCtx{};
        int ret{avformat_open_input(&fmtCtx, reinterpret_cast<const char *>(path.u8string().data()), nullptr, nullptr)};
        require(ret >= 0, Error::FFMPEG_OPEN);
        formatContext.reset(fmtCtx);
        require(avformat_find_stream_info(formatContext.get(), nullptr) >= 0, Error::FFMPEG_STREAM);
        openCodecContext();
        require(!!audioStream, Error::FFMPEG_NOSTREAM); // Setup by openCodecContext.
        require(!!frame, Error::FFMPEG_ALLOC);
        require(!!filteredFrame, Error::FFMPEG_ALLOC);
        require(!!packet, Error::FFMPEG_ALLOC);
        initFilterGraph();
        getNewFFrame(); // Initial frame.
        isValid = true;
    };
};

/// Pairs an audio instance with a Decoder instance, along with a mutex.
/// purely a utility struct.
struct AudioDecoder {
    Decoder &decoder;
    Audio &aud;
    std::mutex mutex{};
    AudioDecoder(Decoder &decoder, Audio &aud) : decoder{decoder}, aud{aud} {};
};

/// Miniaudio callback.
void callback(ma_device *device, void *pOut, const void *pIn, unsigned int framesPerChannel) {
    AudioDecoder &ad{*static_cast<AudioDecoder *>(device->pUserData)};
    AudioState &state{ad.aud.getState()};
    const std::size_t totalSamples{framesPerChannel * Audio::channels};
    std::int16_t *out{static_cast<std::int16_t *>(pOut)};
    if (state.muted.load() || !state.playback.load() || ad.decoder.eof() || !ad.decoder.isDecoderValid()) {
        std::fill(out, out + totalSamples, 0);
        if (ad.decoder.eof() && state.looped.load()) {
            state.conVar.notify_one();
        }
        return;
    }
    for (std::size_t i{}; i < totalSamples; ++i) {
        if (state.rIdx == state.wIdx) {
            out[i] = 0;
            continue;
        }
        out[i] = state.buffer[state.rIdx] * state.volume.load();
        state.rIdx = (state.rIdx + 1) % Audio::sampleBufferSize;
    }
    state.timestamp.store(
        state.timestamp.load() + std::chrono::duration<float>(framesPerChannel / static_cast<float>(Audio::sampleRate))
    );
    state.conVar.notify_one();
}

} // namespace detail

Audio::Audio() {
    if (av_log_get_level() != AV_LOG_ERROR) {
        av_log_set_level(AV_LOG_ERROR);
    }
    state.buffer.resize(Audio::sampleBufferSize);
}

/// Producer thread for the audio class.
void Audio::pthread() {

    detail::Decoder decoder{};
    detail::AudioDecoder ad{decoder, *this};

    // Miniaudio initialization.
    ma_device device{};
    ma_device_config config{};
    config.deviceType = ma_device_type_playback;
    config.pUserData = static_cast<void *>(&ad);
    config.sampleRate = Audio::sampleRate;
    config.playback.channels = Audio::channels;
    config.dataCallback = detail::callback;
    config.playback.format = ma_format_s16;
    require(ma_device_init(nullptr, &config, &device) == MA_SUCCESS, Error::MINIAUDIO);
    require(ma_device_start(&device) == MA_SUCCESS, Error::MINIAUDIO);

    // Lambda definitions.
    std::array<std::function<void(const Command &command)>, szT(CommandType::COUNT)> lambdas{};
    lambdas[szT(CommandType::TOGGLE_LOOP)] = [this](const Command &command) {
        const bool nVal{!state.looped.load()};
        state.looped.store(nVal);
    };
    lambdas[szT(CommandType::TOGGLE_MUTE)] = [this](const Command &command) {
        const bool nVal{!state.muted.load()};
        state.muted.store(nVal);
    };
    lambdas[szT(CommandType::TOGGLE_PLAYBACK)] = [this](const Command &command) {
        const bool nVal{!state.playback.load()};
        state.playback.store(nVal);
    };
    lambdas[szT(CommandType::VOL_SET)] = [this](const Command &command) { state.volume.store(command.fVal); };
    lambdas[szT(CommandType::VOL_UP)] = [this](const Command &command) {
        const float nVal{std::clamp(state.volume.load() + command.fVal, 0.0f, 1.0f)};
        state.volume.store(nVal);
    };
    lambdas[szT(CommandType::VOL_DOWN)] = [this](const Command &command) {
        const float nVal{std::clamp(state.volume.load() - command.fVal, 0.0f, 1.0f)};
        state.volume.store(nVal);
    };
    lambdas[szT(CommandType::SEEK_TO)] = [this, &ad](const Command &command) {
        std::lock_guard<std::mutex> lock{ad.mutex};
        state.timestamp.store(std::chrono::duration<float>(std::clamp(command.fVal, 0.0f, metadata.duration)));
        ad.decoder.decodeAt(state.timestamp);
    };
    lambdas[szT(CommandType::SEEK_BACKWARD)] = [this, &ad](const Command &command) {
        std::lock_guard<std::mutex> lock{ad.mutex};
        state.timestamp.store(
            std::chrono::duration<float>(
                std::clamp(state.timestamp.load().count() - command.fVal, 0.0f, metadata.duration)
            )
        );
        ad.decoder.decodeAt(state.timestamp);
    };
    lambdas[szT(CommandType::SEEK_FORWARD)] = [this, &ad](const Command &command) {
        std::lock_guard<std::mutex> lock{ad.mutex};
        state.timestamp.store(
            std::chrono::duration<float>(
                std::clamp(state.timestamp.load().count() + command.fVal, 0.0f, metadata.duration)
            )
        );
        ad.decoder.decodeAt(state.timestamp);
    };
    lambdas[szT(CommandType::PLAY_ENTRY)] = [this, &ad](const Command &command) {
        std::lock_guard<std::mutex> lock{ad.mutex};
        state.playback.store(true);
        ad.decoder = detail::Decoder{command.ent.asPath()};
        metadata.duration = ad.decoder.getDuration();
        state.timestamp.store(std::chrono::duration<float>(0.0f));
        state.ended.store(false);
    };
    lambdas[szT(CommandType::STOP_CURRENT)] = [this](const Command &command) { state.playback.store(false); };

    std::array<Command, comQueueLen> pendingCommands{};
    std::uint32_t pendingCommandsCount{};
    while (true) {
        {
            std::unique_lock<std::mutex> lock{state.mutex};
            state.conVar.wait(lock, [&] {
                return state.terminate.load() || state.commandR != state.commandW ||
                       (((state.wIdx + 1) % Audio::sampleBufferSize) != state.rIdx) || decoder.eof();
            });
            if (decoder.eof()) {
                state.ended.store(true);
                if (state.looped.load()) {
                    decoder = std::move(detail::Decoder{decoder.getPath()});
                    state.timestamp.store(std::chrono::duration<float>(0.0f));
                    state.ended.store(false);
                }
            }
            while ((state.wIdx + 1) % Audio::sampleBufferSize != state.rIdx) {
                decoder.isDecoderValid() ? (decoder >> state.buffer[state.wIdx], 0) : (state.buffer[state.wIdx] = 0);
                state.wIdx = (state.wIdx + 1) % Audio::sampleBufferSize;
            }
            while (state.commandR != state.commandW) {
                pendingCommands[pendingCommandsCount] = state.commandQueue[state.commandR];
                state.commandR = state.commandR < comQueueLen - 1 ? state.commandR + 1 : 0;
                pendingCommandsCount++;
            }
        }
        for (std::size_t i{}; i < pendingCommandsCount; ++i) {
            lambdas[static_cast<std::size_t>(pendingCommands[i].comType)](pendingCommands[i]);
        }
        if (state.terminate.load()) {
            break;
        }
        pendingCommandsCount = 0;
    }
    ma_device_uninit(&device);
}

/// Sends a command from the main thread to the producer thread.
void Audio::sendCommand(const Command command) {
    {
        std::lock_guard<std::mutex> lock{state.mutex};
        // Drop requests if queue is full.
        if ((state.commandW + 1) % comQueueLen == state.commandR) {
            return;
        }
        state.commandQueue[state.commandW] = command;
        state.commandW = state.commandW < comQueueLen - 1 ? state.commandW + 1 : 0;
    }
    state.conVar.notify_one();
}

/// Audio class entry point.
void Audio::run(const bool loopDefault, const std::uint8_t volume) {
    state.looped.store(loopDefault);
    state.volume.store(volume / 100.0f);
    producerThread = std::thread([this] { this->pthread(); });
}

/*
    Helper functions to simplify API.
*/

void Audio::seekTo(const float v) { sendCommand(Command{CommandType::SEEK_TO, v}); }
void Audio::seekForward(const float v) { sendCommand(Command{CommandType::SEEK_FORWARD, v}); }
void Audio::seekBackward(const float v) { sendCommand(Command{CommandType::SEEK_BACKWARD, v}); }
void Audio::volUp(const float v) { sendCommand(Command{CommandType::VOL_UP, v}); }
void Audio::volDown(const float v) { sendCommand(Command{CommandType::VOL_DOWN, v}); }
void Audio::volSet(const float v) { sendCommand(Command{CommandType::VOL_SET, v}); }
void Audio::playEntry(const Entry &entry) { sendCommand(Command{CommandType::PLAY_ENTRY, 0.0f, entry}); }
void Audio::toggleMute() { sendCommand(Command{CommandType::TOGGLE_MUTE}); }
void Audio::togglePlayback() { sendCommand(Command{CommandType::TOGGLE_PLAYBACK}); }
void Audio::toggleLooping() { sendCommand(Command{CommandType::TOGGLE_LOOP}); }
void Audio::stopCurrent() { sendCommand(Command{CommandType::STOP_CURRENT}); }

} // namespace tml
