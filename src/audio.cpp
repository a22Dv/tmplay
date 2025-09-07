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
#include <libavutil/mathematics.h>
#include <libavutil/mem.h>
#include <libavutil/opt.h>
#include <libavutil/samplefmt.h>
}

#include <array>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

#include "player.hpp"
#include "utils.hpp"

namespace tml {

Audio::~Audio() {
    state.terminate.store(true);
    state.conVar.notify_one();
    if (producerThread.joinable()) {
        producerThread.join();
    }
}

namespace detail {

/**
    TODO:
    Major memory leaks during exception paths.
    For now focus on the non-exception paths, but refactor
    afterwards once this works and meets expectations.
*/
enum class DecoderStatus : std::uint8_t { SUCCESS, END_OF_FILE, EXCEPTION, AGAIN, END_OF_BUFFER };
class Decoder {

    // isEof refers to the entire pipeline reaching the end, signaledEof is an internal boolean for managing state.
    bool isEof{};
    bool signaledEof{};
    bool isValid{};
    static constexpr float latencyMs{1000.0f / 20.0f};
    static constexpr const char *filterDescription{"aresample=48000,aformat=sample_fmts=s16:channel_layouts=stereo"};
    AVFormatContext *formatContext{};
    AVCodecContext *codecContext{};
    AVFilterContext *bSinkContext{};
    AVFilterContext *bSrcContext{};
    AVFilterGraph *filterGraph{};
    AVStream *audioStream{};
    AVPacket *packet{};
    AVFrame *frame{};
    AVFrame *filteredFrame{};
    int aStreamIdx{-1};
    fs::path filepath{};
    std::chrono::duration<float> timestamp{};
    std::vector<std::int16_t> stagingBuffer{};
    std::size_t stagingBufferIdx{};

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
        int ret{av_find_best_stream(formatContext, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0)};
        require(ret >= 0, Error::FFMPEG_STREAM);
        aStreamIdxL = ret;
        stream = formatContext->streams[aStreamIdxL];
        codec = avcodec_find_decoder(stream->codecpar->codec_id);
        require(codec, Error::FFMPEG_DECODER);
        codecContext = avcodec_alloc_context3(codec);
        require(codecContext, Error::FFMPEG_CONTEXT);
        ret = avcodec_parameters_to_context(codecContext, stream->codecpar);
        require(ret >= 0, Error::FFMPEG_CONTEXT);
        ret = avcodec_open2(codecContext, codec, nullptr);
        require(ret >= 0, Error::FFMPEG_DECODER);
        aStreamIdx = aStreamIdxL;
    }
    void avFree() {
        av_frame_free(&frame);
        av_frame_free(&filteredFrame);
        av_packet_free(&packet);
        avfilter_graph_free(&filterGraph);
        avcodec_free_context(&codecContext);
        avformat_close_input(&formatContext);
        isValid = false;
        aStreamIdx = -1;
        audioStream = nullptr;
    }
    void moveImpl(Decoder &&other) noexcept {
        isValid = other.isValid;
        formatContext = other.formatContext;
        codecContext = other.codecContext;
        bSinkContext = other.bSinkContext;
        bSrcContext = other.bSrcContext;
        filterGraph = other.filterGraph;
        audioStream = other.audioStream;
        frame = other.frame;
        packet = other.packet;
        aStreamIdx = other.aStreamIdx;
        filepath = std::move(other.filepath);
        timestamp = std::move(other.timestamp);
        stagingBuffer = std::move(other.stagingBuffer);
        other.isValid = false;
        other.formatContext = nullptr;
        other.codecContext = nullptr;
        other.bSinkContext = nullptr;
        other.bSrcContext = nullptr;
        other.filterGraph = nullptr;
        other.audioStream = nullptr;
    }
    void initFilterGraph() {
        /*
            Initializes the filter graph pipeline.
            This is responsible for turning audio data into an
            expected format (e.g. 48kHz, Stereo).
        */
        std::string arguments{};
        const AVFilter *src{avfilter_get_by_name("abuffer")};
        const AVFilter *sink{avfilter_get_by_name("abuffersink")};
        const AVFilterLink *link{};
        const int sampleRate{Audio::sampleRate};
        AVRational timeBase{audioStream->time_base};
        AVFilterInOut *in{avfilter_inout_alloc()};
        AVFilterInOut *out{avfilter_inout_alloc()};
        filterGraph = avfilter_graph_alloc();
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
        int ret{avfilter_graph_create_filter(&bSrcContext, src, "in", arguments.c_str(), NULL, filterGraph)};
        require(ret >= 0, Error::FFMPEG_FILTER);
        bSinkContext = avfilter_graph_alloc_filter(filterGraph, sink, "out");
        require(bSinkContext, Error::FFMPEG_FILTER);
        require(av_opt_set(bSinkContext, "sample_formats", "s16", AV_OPT_SEARCH_CHILDREN), Error::FFMPEG_FILTER);
        require(av_opt_set(bSinkContext, "channel_layouts", "stereo", AV_OPT_SEARCH_CHILDREN), Error::FFMPEG_FILTER);
        require(
            av_opt_set_array(bSinkContext, "samplerates", AV_OPT_SEARCH_CHILDREN, 0, 1, AV_OPT_TYPE_INT, &sampleRate),
            Error::FFMPEG_FILTER
        );
        require(avfilter_init_dict(bSinkContext, NULL), Error::FFMPEG_FILTER);

        // Swapped due to the fact these specify connections.
        // Not something within the filter itself.
        in->name = const_cast<char *>("out");
        in->filter_ctx = bSinkContext;
        in->pad_idx = 0;
        in->next = nullptr;
        out->name = const_cast<char *>("in");
        out->filter_ctx = bSrcContext;
        out->pad_idx = 0;
        out->next = nullptr;

        require(
            avfilter_graph_parse_ptr(filterGraph, filterDescription, &in, &out, nullptr) >= 0, Error::FFMPEG_FILTER
        );
        require(avfilter_graph_config(filterGraph, nullptr) >= 0, Error::FFMPEG_FILTER);
        avfilter_inout_free(&in);
        avfilter_inout_free(&out);
    }
    /*
        AVRational is a fraction, which is why despite the formula being implemented as a * (b / c),
        where b is the destination unit, and c is the source unit, because these are fractions,
        we have to reverse them. In that a * ((1 / c) / (1 / b)) = a * (b / c). This is a simplification
        however, but that's the gist of it.
    */
    std::chrono::duration<float> fromStreamTicks(const int ticks) {
        // Convert from AVStream.time_base ticks to AV_TIME_BASE ticks. Then convert back to seconds.
        std::int64_t timeBase{av_rescale_q(ticks, audioStream->time_base, AVRational{1, AV_TIME_BASE})};
        return std::chrono::duration<float>{static_cast<float>(timeBase) / AV_TIME_BASE};
    }
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
    DecoderStatus retrieveFrame() {
        int ret{};
        if ((ret = avcodec_receive_frame(codecContext, frame)) < 0) {
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
    DecoderStatus retrieveFFrame() {
        int ret{};
        if ((ret = av_buffersink_get_frame(bSinkContext, filteredFrame)) < 0) {
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
    DecoderStatus retrieveSample(std::int16_t &sample) {
        if (stagingBufferIdx < stagingBuffer.size()) {
            sample = stagingBuffer[stagingBufferIdx++];
            return DecoderStatus::SUCCESS;
        }
        stagingBuffer.clear();
        stagingBufferIdx = 0;
        return DecoderStatus::END_OF_BUFFER;
    };
    void pushToStagingBuffer() {
        std::int16_t* frameData{reinterpret_cast<std::int16_t*>(filteredFrame->data[0])};
        std::size_t samplesInFrame{filteredFrame->nb_samples * Audio::channels};
        stagingBuffer.insert(stagingBuffer.end(), &frameData[0], &frameData[samplesInFrame]);
    };
    /// TODO:
    DecoderStatus getNewFFrame() {
        return DecoderStatus::EXCEPTION;
    }
    DecoderStatus getNewFrame() {
        return DecoderStatus::EXCEPTION;
    }
    DecoderStatus getNewSample(std::int16_t &sample) {
        return DecoderStatus::EXCEPTION;
    };

  public:
    bool isDecoderValid() { return isValid; };
    const fs::path &getPath() const { return filepath; };
    const std::vector<std::int16_t> &getBuffer() const { return stagingBuffer; };
    void decodeAt(std::chrono::duration<float> trgtTime) {
        avcodec_flush_buffers(codecContext);
        while ((av_frame_unref(filteredFrame), true) && retrieveFFrame() == DecoderStatus::SUCCESS) {
            continue;
        }
        const std::int64_t targetTicks{toStreamTicks(trgtTime)};
        av_seek_frame(formatContext, aStreamIdx, targetTicks, 0);
        while (filteredFrame->pts < targetTicks) {
            getNewFFrame();
        }
    };
    Decoder &operator>>(int16_t &sample) {
        DecoderStatus status{getNewSample(sample)};
        require(status != DecoderStatus::EXCEPTION, Error::FFMPEG_DECODER);
        if (status != DecoderStatus::SUCCESS) {
            sample = 0;
        }
        return *this;
    }
    explicit operator bool() const { return isEof; }
    bool eof() const { return isEof; };
    Decoder() {};
    Decoder(const fs::path &path) : filepath{path} {
        /*
            Open input, setup format context.
            Find stream, setup codec context, allocate for frames,
            setup filter graph.
        */
        stagingBuffer.resize(Audio::sampleRate * (latencyMs / 1000.0f) * Audio::channels);
        require(
            avformat_open_input(
                &formatContext, reinterpret_cast<const char *>(path.u8string().data()), nullptr, nullptr
            ) >= 0,
            Error::FFMPEG_OPEN
        );
        require(avformat_find_stream_info(formatContext, nullptr) >= 0, Error::FFMPEG_STREAM);
        openCodecContext();
        audioStream = formatContext->streams[aStreamIdx];
        require(audioStream, Error::FFMPEG_NOSTREAM);
        frame = av_frame_alloc();
        filteredFrame = av_frame_alloc();
        packet = av_packet_alloc();
        require(frame, Error::FFMPEG_ALLOC);
        require(filteredFrame, Error::FFMPEG_ALLOC);
        require(packet, Error::FFMPEG_ALLOC);
        initFilterGraph();
        isValid = true;
    };
    Decoder(const Decoder &) = delete;
    Decoder &operator=(const Decoder &) = delete;
    Decoder(Decoder &&other) noexcept { moveImpl(std::move(other)); };
    Decoder &operator=(Decoder &&other) noexcept {
        if (this == &other) {
            return *this;
        }
        avFree();
        moveImpl(std::move(other));
        return *this;
    };
    ~Decoder() { avFree(); };
};

void callback(ma_device *device, void *pOut, const void *pIn, unsigned int framesPerChannel) {
    Audio &audio{*static_cast<Audio *>(device->pUserData)};
    const std::size_t totalSamples{framesPerChannel * Audio::channels};
}

} // namespace detail

void Audio::pthread() {

    // Miniaudio initialization.
    ma_device device{};
    ma_device_config config{};
    config.deviceType = ma_device_type_playback;
    config.pUserData = this;
    config.sampleRate = Audio::sampleRate;
    config.playback.channels = Audio::channels;
    config.dataCallback = detail::callback;
    config.playback.format = ma_format_s16;
    require(ma_device_init(nullptr, &config, &device) == MA_SUCCESS, Error::MINIAUDIO);
    require(ma_device_start(&device) == MA_SUCCESS, Error::MINIAUDIO);

    detail::Decoder decoder{};

    // Lambda definitions.
    std::array<std::function<void(const Command &command)>, szT(CommandType::COUNT)> lambdas{};
    lambdas[szT(CommandType::TOGGLE_LOOP)] = [this](const Command &command) {
        const bool nVal{!state.looped.load()};
        state.looped.store(nVal);
    };
    lambdas[szT(CommandType::TOGGLE_MUTE)] = [this](const Command &command) {
        const bool nVal{state.muted.load()};
        state.muted.store(nVal);
    };
    lambdas[szT(CommandType::TOGGLE_PLAYBACK)] = [this](const Command &command) {
        const bool nVal{state.playback.load()};
        state.playback.store(nVal);
    };
    lambdas[szT(CommandType::VOL_SET)] = [this](const Command &command) { state.volume.store(command.fVal); };
    lambdas[szT(CommandType::VOL_UP)] = [this](const Command &command) {
        const float nVal{state.volume.load() + command.fVal};
        state.volume.store(nVal);
    };
    lambdas[szT(CommandType::VOL_DOWN)] = [this](const Command &command) {
        const float nVal{state.volume.load() - command.fVal};
        state.volume.store(nVal);
    };
    lambdas[szT(CommandType::SEEK_TO)] = [this](const Command &command) {
        state.timestamp = std::chrono::duration<float>(command.fVal);
        state.serial++;
    };
    lambdas[szT(CommandType::SEEK_BACKWARD)] = [this](const Command &command) {
        state.timestamp = state.timestamp.load() - std::chrono::duration<float>(command.fVal);
        state.serial++;
    };
    lambdas[szT(CommandType::SEEK_FORWARD)] = [this](const Command &command) {
        state.timestamp = state.timestamp.load() + std::chrono::duration<float>(command.fVal);
        state.serial++;
    };
    lambdas[szT(CommandType::PLAY_ENTRY)] = [this, &decoder](const Command &command) {
        state.playback.store(true);
        decoder = detail::Decoder{command.ent.asPath()};
    };
    lambdas[szT(CommandType::STOP_CURRENT)] = [this](const Command &command) { state.playback.store(false); };

    std::array<Command, comQueueLen> pendingCommands{};
    std::uint32_t pendingCommandsCount{};
    while (true) {
        {
            std::unique_lock<std::mutex> lock{state.mutex};
            state.conVar.wait(lock, [&] { return state.terminate.load() || state.commandR != state.commandW; });
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
    }
    ma_device_uninit(&device);
}

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

void Audio::run() {
    producerThread = std::thread([this] { this->pthread(); });
}

void Audio::seekForward(const float v) { sendCommand(Command{CommandType::SEEK_FORWARD, v}); }
void Audio::seekBackward(const float v) { sendCommand(Command{CommandType::SEEK_BACKWARD, v}); }
void Audio::volUp(const float v) { sendCommand(Command{CommandType::VOL_UP, v}); }
void Audio::volDown(const float v) { sendCommand(Command{CommandType::VOL_DOWN, v}); }
void Audio::volSet(const float v) { sendCommand(Command{CommandType::VOL_SET, v}); }
void Audio::playEntry(const Entry &entry, const float v) { sendCommand(Command{CommandType::PLAY_ENTRY, v, entry}); }
void Audio::toggleMute() { sendCommand(Command{CommandType::TOGGLE_MUTE}); }
void Audio::togglePlayback() { sendCommand(Command{CommandType::TOGGLE_PLAYBACK}); }
void Audio::toggleLooping() { sendCommand(Command{CommandType::TOGGLE_PLAYBACK}); }
void Audio::stopCurrent() { sendCommand(Command{CommandType::STOP_CURRENT}); }

} // namespace tml
