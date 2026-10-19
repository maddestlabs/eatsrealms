#define MINIAUDIO_IMPLEMENTATION
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#endif
#include "miniaudio.h"
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#endif
#include "eatsrealms/audio_foley.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace eatsrealms {

namespace {

void maDataCallback(ma_device* pDevice, void* pOutput, const void* /*pInput*/, ma_uint32 frameCount) {
    auto* engine = static_cast<AudioFoleyEngine*>(pDevice->pUserData);
    if (!engine) return;
    engine->mixSamples(static_cast<float*>(pOutput), frameCount, pDevice->sampleRate);
}

} // namespace

AudioFoleyEngine::~AudioFoleyEngine() {
    shutdown();
}

bool AudioFoleyEngine::init(bool enableHardwareDevice) {
    if (initialized_) return true;
    if (!enableHardwareDevice) {
        initialized_ = true;
        return true;
    }

    auto* device = new ma_device();
    ma_device_config config = ma_device_config_init(ma_device_type_playback);
    config.playback.format   = ma_format_f32;
    config.playback.channels = 2;
    config.sampleRate        = 48000;
    config.dataCallback      = maDataCallback;
    config.pUserData         = this;

    if (ma_device_init(nullptr, &config, device) != MA_SUCCESS) {
        delete device;
        initialized_ = true;
        return true;
    }

    ma_device_start(device);
    maDevicePtr_ = device;
    initialized_ = true;
    return true;
}

void AudioFoleyEngine::shutdown() {
    if (maDevicePtr_) {
        auto* device = static_cast<ma_device*>(maDevicePtr_);
        ma_device_uninit(device);
        delete device;
        maDevicePtr_ = nullptr;
    }
    initialized_ = false;
}

void AudioFoleyEngine::triggerMaterialImpact(uint16_t layerId, float intensity) {
    int slot = 0;
    float lowestEnv = 999.0f;
    for (int i = 0; i < kMaxVoices; ++i) {
        if (!voices_[i].active.load(std::memory_order_relaxed)) {
            slot = i;
            break;
        }
        if (voices_[i].env < lowestEnv) {
            lowestEnv = voices_[i].env;
            slot = i;
        }
    }

    Voice& v = voices_[slot];
    v.active.store(false, std::memory_order_relaxed);
    v.layerId = layerId;
    v.phase1 = 0.0f;
    v.phase2 = 0.0f;
    v.filterState = 0.0f;
    v.env = std::clamp(intensity, 0.1f, 1.0f) * 0.35f;

    switch (layerId) {
        case TexLayer::TerracottaRoof:
        case TexLayer::SlateRoof:
            v.freq1 = 1140.0f;
            v.freq2 = 2680.0f;
            v.decay = 0.9982f;
            v.noiseMix = 0.15f;
            break;
        case TexLayer::WaterSurface:
            v.freq1 = 520.0f;
            v.freq2 = 1280.0f;
            v.decay = 0.9990f;
            v.noiseMix = 0.72f;
            break;
        case TexLayer::Cobblestone:
        case TexLayer::CliffStone:
        case TexLayer::StoneBrick:
            v.freq1 = 190.0f;
            v.freq2 = 435.0f;
            v.decay = 0.9972f;
            v.noiseMix = 0.36f;
            break;
        case TexLayer::GrassMeadow:
        case TexLayer::GrassHighland:
        case TexLayer::SpritePineTree:
        case TexLayer::SpriteOakTree:
        case TexLayer::SpriteWildflower:
            v.freq1 = 315.0f;
            v.freq2 = 650.0f;
            v.decay = 0.9991f;
            v.noiseMix = 0.86f;
            break;
        default:
            v.freq1 = 410.0f;
            v.freq2 = 960.0f;
            v.decay = 0.9978f;
            v.noiseMix = 0.22f;
            break;
    }

    v.active.store(true, std::memory_order_release);
}

void AudioFoleyEngine::mixSamples(float* outInterleavedStereo, uint32_t frameCount, uint32_t sampleRate) noexcept {
    std::memset(outInterleavedStereo, 0, frameCount * 2 * sizeof(float));
    const float invSr = 1.0f / static_cast<float>(sampleRate);
    constexpr float kTwoPi = 6.283185307f;

    for (uint32_t f = 0; f < frameCount; ++f) {
        float mix = 0.0f;
        for (int i = 0; i < kMaxVoices; ++i) {
            Voice& v = voices_[i];
            if (!v.active.load(std::memory_order_acquire)) continue;

            rngState_ = rngState_ * 1664525u + 1013904223u;
            float white = (static_cast<float>(rngState_ & 0x00FFFFFFu) / static_cast<float>(0x00800000u)) - 1.0f;
            v.filterState = v.filterState * 0.82f + white * 0.18f;

            float tone = std::sin(v.phase1 * kTwoPi) * 0.65f + std::sin(v.phase2 * kTwoPi) * 0.35f;
            v.phase1 += v.freq1 * invSr;
            if (v.phase1 >= 1.0f) v.phase1 -= 1.0f;
            v.phase2 += v.freq2 * invSr;
            if (v.phase2 >= 1.0f) v.phase2 -= 1.0f;

            float sig = (tone * (1.0f - v.noiseMix) + v.filterState * v.noiseMix) * v.env;
            mix += sig;

            v.env *= v.decay;
            if (v.env < 0.0002f) {
                v.active.store(false, std::memory_order_release);
            }
        }

        mix = std::clamp(mix, -0.95f, 0.95f);
        outInterleavedStereo[f * 2 + 0] = mix;
        outInterleavedStereo[f * 2 + 1] = mix;
    }
}

} // namespace eatsrealms
