#include "maudio.hpp"

#include <algorithm>
#include <cstdint>

namespace trm {

AudioDevice::AudioDevice() {
    device.devConfig.playback.format = MaDeviceSpecifiers::format;
    device.devConfig.playback.channels = MaDeviceSpecifiers::channels;
    device.devConfig.sampleRate = MaDeviceSpecifiers::sampleRate;
    device.devConfig.deviceType = MaDeviceSpecifiers::deviceType;
    device.devConfig.dataCallback = device.callback;
    device.devConfig.pUserData = static_cast<void *>(this);
    ma_device_init(nullptr, &device.devConfig, &device.dev);
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

void AudioDevice::start([[maybe_unused]] const Command &command) { return; }

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
                       this->state.sampleQueue.size() < MaDeviceSpecifiers::queueLimit;
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
        while (state.sampleQueue.size() < MaDeviceSpecifiers::queueLimit) {
            /// TODO: Decoder sample retrieval.
        }
        if (state.terminate) {
            break;
        }
    }
}

void MaDevice::callback(
    ma_device *device, void *out, [[maybe_unused]] const void *in, unsigned int frames
) {
    AudioDevice &aDevice{*static_cast<AudioDevice *>(device->pUserData)};
    DeviceState &state{aDevice.state};
    const std::uint32_t tFrameCount{frames * MaDeviceSpecifiers::channels};
    std::uint32_t framesServed{};
    std::int16_t *sampleOut{static_cast<std::int16_t *>(out)};
    if (state.muted || !state.playback || state.sampleQueue.empty() || !state.ready) {
        std::fill(sampleOut, sampleOut + tFrameCount, 0);
        return;
    }
    {
        std::lock_guard<std::mutex> lock{aDevice.state.queueMutex};
        while (!state.sampleQueue.empty() && framesServed != tFrameCount) {
            *(sampleOut + framesServed) = state.sampleQueue.front();
            state.sampleQueue.pop();
            ++framesServed;
        }
    }
    if (framesServed != tFrameCount) [[unlikely]] {
        std::fill(sampleOut + framesServed, sampleOut + tFrameCount, 0);
    }
}

} // namespace trm
