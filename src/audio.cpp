#include "utils.hpp"
#include <libavfilter/avfilter.h>
#define MINIAUDIO_IMPLEMENTATION

#include <array>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <thread>

#include "miniaudio.h"
#include "player.hpp"
#include <libavcodec/avcodec.h>
#include <libavfilter/buffersink.h>
#include <libavfilter/buffersrc.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavutil/mem.h>
#include <libavutil/opt.h>

namespace tml {

Audio::~Audio() {
    state.terminate.store(true);
    state.conVar.notify_one();
    if (producerThread.joinable()) {
        producerThread.join();
    }
}

namespace detail {

void callback(ma_device *device, void *pOut, const void *pIn, unsigned int framesPerChannel) {}

/**
    TODO:
    Free contexts
    Implement actual logic.
    Review for any bugs.
    Move semantics.
*/
class Decoder {
    static constexpr float latencyMs{1000.0f / 20.0f};
    AVFormatContext *formatContext{};
    AVCodecContext *codecContext{};
    AVFilterContext *bsinkContext{};
    AVFilterContext *bsrcContext{};
    AVFilterGraph *filterGraph{};
    int aStreamIdx{-1};
    fs::path filepath{};
    std::chrono::duration<float> timestamp{};
    std::vector<std::int16_t> stagingBuffer{};

  public:
    const fs::path &getPath() const { return filepath; };
    const std::vector<std::int16_t> &getBuffer() const { return stagingBuffer; };
    void decodeAt(std::chrono::duration<float> trgtTime) { timestamp = trgtTime; };
    void decodeNext() {
        timestamp += std::chrono::duration<float>(latencyMs / 1000.0f);
        return;
    };
    Decoder() {};
    Decoder(const fs::path &path) : filepath{path} {
        stagingBuffer.resize(Audio::sampleRate * (latencyMs / 1000.0f) * Audio::channels);
    };
    Decoder(const Decoder &) = delete;
    Decoder &operator=(const Decoder &) = delete;
    Decoder(Decoder &&other) {
        return;
    };
    Decoder &operator=(Decoder &&other) { return other; };
    ~Decoder() {};
};

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
