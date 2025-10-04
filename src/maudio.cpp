#include "maudio.hpp"

#include <cstdint>
#include <algorithm>

namespace trm {

AudioDevice::AudioDevice() {
    device.devConfig.playback.format = devSpec.format;
    device.devConfig.playback.channels = devSpec.channels;
    device.devConfig.sampleRate = devSpec.sampleRate;
    device.devConfig.deviceType = devSpec.deviceType;
    device.devConfig.dataCallback = device.callback;
    device.devConfig.pUserData = static_cast<void*>(this);
    ma_device_init(nullptr, &device.devConfig, &device.dev);
}

AudioDevice::~AudioDevice() {
   ma_device_uninit(&device.dev);
}

void MaDevice::callback(ma_device *device, void *out, [[maybe_unused]] const void *in, unsigned int frames) {
    AudioDevice& aDevice{*static_cast<AudioDevice*>(device->pUserData)};
    std::uint32_t tFrameCount{frames * aDevice.devSpec.channels};
    std::int16_t* sampleOut{static_cast<std::int16_t*>(out)};
    if (aDevice.state.muted || !aDevice.state.playback || !aDevice.state.ready) {
        std::fill(sampleOut, sampleOut + tFrameCount, 0);
    }
}

}
