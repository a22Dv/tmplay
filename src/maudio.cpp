#define MINIAUDIO_IMPLEMENTATION

#include <algorithm>
#include <cstdint>

#include "maudio.hpp"
#include "utils.hpp"

namespace trm {

AudioDevice::AudioDevice() {
    device.devConfig.playback.format = MaDeviceSpecifiers::format;
    device.devConfig.playback.channels = MaDeviceSpecifiers::channels;
    device.devConfig.sampleRate = MaDeviceSpecifiers::sampleRate;
    device.devConfig.deviceType = MaDeviceSpecifiers::deviceType;
    device.devConfig.dataCallback = device.callback;
    device.devConfig.pUserData = static_cast<void *>(this);

    require(ma_device_init(nullptr, &device.devConfig, &device.dev) == MA_SUCCESS, Error::MA_INIT);
    require(ma_device_start(&device.dev) == MA_SUCCESS, Error::MA_INIT);

    internalThread = std::thread([this] { this->pThread(); });
}

AudioDevice::~AudioDevice() {
    if (internalThread.joinable()) {
        internalThread.join();
    }
    ma_device_uninit(&device.dev);
}

void AudioDevice::play([[maybe_unused]] const Command &command) { state.playback.store(true); }

void AudioDevice::pause([[maybe_unused]] const Command &command) { state.playback.store(false); }

void AudioDevice::togglePlayback([[maybe_unused]] const Command &command) {
    state.playback.store(!state.playback.load());
}

void AudioDevice::toggleMute([[maybe_unused]] const Command &command) {
    state.muted.store(!state.muted.load());
}

void AudioDevice::toggleLooping([[maybe_unused]] const Command &command) {
    state.looping.store(!state.looping.load());
}

void AudioDevice::seekTo(const Command &command) {
    state.timestamp.store(command.fVal.value_or(0.0f));
}

void AudioDevice::setVol(const Command &command) {
    state.volume.store(command.fVal.value_or(0.0f));
}

void AudioDevice::incVol(const Command &command) {
    state.volume.store(std::min(1.0f, state.volume.load() + command.fVal.value_or(0.0f)));
}

void AudioDevice::decVol(const Command &command) {
    state.volume.store(std::max(0.0f, state.volume.load() - command.fVal.value_or(0.0f)));
}

void AudioDevice::start([[maybe_unused]] const Command &command) {
    state.timestamp.store(0.0f);
    state.ready.store(true);
    /// TODO: Act on pVal (path).
}

void AudioDevice::end([[maybe_unused]] const Command &command) {
    state.playback.store(false);
    state.ready.store(false);
    state.timestamp.store(0.0f);
}

void AudioDevice::pThread() {
    while (true) {
        {
            std::unique_lock<std::mutex> lock{state.commandMutex};
            state.condition.wait(lock, [this] {
                return this->state.terminate.load() || !this->state.commandQueue.empty() ||
                       this->state.cQueueSamples.load() < MaDeviceSpecifiers::queueLimit;
            });
            while (!state.commandQueue.empty()) {
                Command &com{state.commandQueue.front()};
                switch (com.commandType) {
                case CommandType::PLAY: play(com); break;
                case CommandType::PAUSE: pause(com); break;
                case CommandType::TOGGLE_PLAYBACK: togglePlayback(com); break;
                case CommandType::TOGGLE_MUTE: toggleMute(com); break;
                case CommandType::TOGGLE_LOOPING: toggleLooping(com); break;
                case CommandType::SEEK_TO: seekTo(com); break;
                case CommandType::START: start(com); break;
                case CommandType::END: end(com); break;
                case CommandType::SET_VOL: setVol(com); break;
                case CommandType::INC_VOL: incVol(com); break;
                case CommandType::DEC_VOL: decVol(com); break;
                case CommandType::NULL_T: break;
                }
                state.commandQueue.pop();
            }
        }
        {
            std::lock_guard<std::mutex> lock{state.queueMutex};
            while (state.sampleQueue.size() < MaDeviceSpecifiers::queueLimit) {
                break;
            }
        }
        if (state.terminate) {
            break;
        }
    }
}

void AudioDevice::sendCommand(const Command &command) {
    constexpr std::size_t commandLimit{5};
    std::lock_guard<std::mutex> lock{state.commandMutex};
    if (state.commandQueue.size() >= commandLimit) {
        return;
    }
    state.commandQueue.push(command);
    state.condition.notify_one();
}

void AudioDevice::play() {
    Command com{.commandType = CommandType::PLAY};
    sendCommand(com);
}
void AudioDevice::pause() {
    Command com{.commandType = CommandType::PAUSE};
    sendCommand(com);
}
void AudioDevice::togglePlayback() {
    Command com{.commandType = CommandType::TOGGLE_PLAYBACK};
    sendCommand(com);
}
void AudioDevice::toggleMute() {
    Command com{.commandType = CommandType::TOGGLE_MUTE};
    sendCommand(com);
}
void AudioDevice::toggleLooping() {
    Command com{.commandType = CommandType::TOGGLE_LOOPING};
    sendCommand(com);
}
void AudioDevice::setVol(const float vol) {
    Command com{.commandType = CommandType::SET_VOL, .fVal = vol};
    sendCommand(com);
}
void AudioDevice::incVol(const float vol) {
    Command com{.commandType = CommandType::INC_VOL, .fVal = vol};
    sendCommand(com);
}
void AudioDevice::decVol(const float vol) {
    Command com{.commandType = CommandType::DEC_VOL, .fVal = vol};
    sendCommand(com);
}
void AudioDevice::seekTo(const float timestamp) {
    Command com{.commandType = CommandType::SEEK_TO, .fVal = timestamp};
    sendCommand(com);
}
void AudioDevice::start(const std::filesystem::path path) {
    Command com{.commandType = CommandType::START, .pVal = path};
    sendCommand(com);
}
void AudioDevice::end() {
    Command com{.commandType = CommandType::END};
    sendCommand(com);
}

void MaDevice::callback(
    ma_device *device, void *out, [[maybe_unused]] const void *in, unsigned int frames
) {
    AudioDevice &aDevice{*static_cast<AudioDevice *>(device->pUserData)};
    DeviceState &state{aDevice.state};
    const std::uint32_t tFrameCount{frames * MaDeviceSpecifiers::channels};
    std::uint32_t framesServed{};
    std::int16_t *sampleOut{static_cast<std::int16_t *>(out)};
    if (state.muted || !state.playback || !state.cQueueSamples.load() || !state.ready) {
        std::fill(sampleOut, sampleOut + tFrameCount, 0);
        return;
    }
    {
        std::lock_guard<std::mutex> lock{aDevice.state.queueMutex};
        while (!state.sampleQueue.empty() && framesServed != tFrameCount) {
            *(sampleOut + framesServed) = state.sampleQueue.front();
            state.qPopSync();
            ++framesServed;
        }
    }
    if (framesServed != tFrameCount) [[unlikely]] {
        std::fill(sampleOut + framesServed, sampleOut + tFrameCount, 0);
    }
    state.condition.notify_one();
}

// Not thread-safe. Modifies the state queue. Must be used within a mutex.
void DeviceState::qPushSync(std::int16_t sample) {
    sampleQueue.push(sample);
    ++cQueueSamples;
}

// Not thread-safe. Modifies the state queue. Must be used within a mutex.
void DeviceState::qPopSync() {
    sampleQueue.pop();
    --cQueueSamples;
}

} // namespace trm
