#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <optional>
#include <queue>


#include "miniaudio.h"

namespace trm {

// Device playback specifiers.
struct MaDeviceSpecifiers {
    static constexpr ma_format format{ma_format_s16};
    static constexpr ma_uint32 channels{2};
    static constexpr ma_uint32 sampleRate{48000};
    static constexpr ma_device_type deviceType{ma_device_type_playback};
    static constexpr std::chrono::milliseconds queueLimitMs{100};
    static constexpr std::size_t queueLimit{static_cast<std::size_t>(
        sampleRate * channels * (static_cast<float>(queueLimitMs.count()) / 1000)
    )};
};

// Miniaudio device.
struct MaDevice {
    ma_device_config devConfig{};
    ma_device dev{};
    static void callback(ma_device *device, void *out, const void *in, unsigned int frames);
};

// Audio command types.
enum class CommandType : std::uint8_t {
    PLAY,
    PAUSE,
    TOGGLE_PLAYBACK,
    TOGGLE_MUTE,
    TOGGLE_LOOPING,
    SET_VOL,
    INC_VOL,
    DEC_VOL,
    SEEK_TO,
    START,
    END,
    NULL_T
};

// Audio command.
struct Command {
    CommandType commandType{CommandType::NULL_T};
    std::optional<bool> bVal{};
    std::optional<float> fVal{};
    std::optional<std::uint64_t> uVal{};
    std::optional<std::filesystem::path> pVal{};
};

// Device state.
struct DeviceState {
    std::atomic<bool> playback{};
    std::atomic<bool> muted{};
    std::atomic<bool> ready{};
    std::atomic<bool> terminate{};
    std::atomic<bool> looping{};
    std::atomic<float> volume{};
    std::atomic<float> timestamp{};
    std::atomic<std::size_t> cQueueSamples{};
    std::mutex queueMutex{};
    std::mutex commandMutex{};
    std::queue<Command> commandQueue{};
    std::queue<std::int16_t> sampleQueue{};
    std::condition_variable condition{};
    void qPushSync(std::int16_t sample);
    void qPopSync();
};


// Playback device.
class AudioDevice {
    MaDevice device{};
    DeviceState state{};
    std::thread internalThread{};
    void sendCommand(const Command& command);
    void pThread();
    void play(const Command& command);
    void pause(const Command& command);
    void togglePlayback(const Command& command);
    void toggleMute(const Command& command);
    void toggleLooping(const Command& command);
    void setVol(const Command& command);
    void incVol(const Command& command);
    void decVol(const Command& command);
    void seekTo(const Command& command);
    void start(const Command& command);
    void end(const Command& command);
    friend struct MaDevice;

  public:
    std::mutex &getQueueMutex() { return state.queueMutex; }
    std::queue<std::int16_t> &getQueue() { return state.sampleQueue; }
    AudioDevice();
    void play();
    void pause();
    void togglePlayback();
    void toggleMute();
    void toggleLooping();
    void setVol(const float vol);
    void incVol(const float vol);
    void decVol(const float vol);
    void seekTo(const float timestamp);
    void start(const std::filesystem::path path);
    void end();
    ~AudioDevice();
};

} // namespace trm