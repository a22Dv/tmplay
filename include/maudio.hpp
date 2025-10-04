#pragma once

#include <atomic>

#include "miniaudio.h"

namespace trm {

// Device playback specifiers.
struct MaDeviceSpecifiers {
    static constexpr ma_format format{ma_format_s16};
    static constexpr ma_uint32 channels{2};
    static constexpr ma_uint32 sampleRate{48000};
    static constexpr ma_device_type deviceType{ma_device_type_playback};
};

// Miniaudio device.
struct MaDevice {
    ma_device_config devConfig{};
    ma_device dev{};
    static void callback(ma_device* device, void* out, const void* in, unsigned int frames);
};

// Device state.
struct DeviceState {
    std::atomic<bool> playback{};
    std::atomic<bool> muted{};
    std::atomic<bool> ready{};
    std::atomic<float> volume{};
};

// Playback device.
class AudioDevice {
    MaDeviceSpecifiers devSpec{};
    MaDevice device{};
    DeviceState state{};
    friend struct MaDevice;

  public:
    AudioDevice();
    ~AudioDevice();
};

} // namespace trm