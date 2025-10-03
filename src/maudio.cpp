#include "maudio.hpp"

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

void MaDevice::callback(ma_device *device, void *out, const void *in, unsigned int frames) {
    return;
}

}
