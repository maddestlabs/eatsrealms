#ifndef EATSREALMS_AUDIO_FOLEY_HPP
#define EATSREALMS_AUDIO_FOLEY_HPP

#include "eatsrealms/hd2d_types.hpp"
#include <atomic>
#include <cstdint>

namespace eatsrealms {

/**
 * Material-Aware Procedural Foley & RPG Sound Synthesizer built on miniaudio.
 * Synthesizes tactile cobblestone footsteps, water splashes, timber creaks,
 * ceramic roof taps, and foliage rustles with zero external audio files.
 */
class AudioFoleyEngine {
public:
    AudioFoleyEngine() = default;
    ~AudioFoleyEngine();

    bool init(bool enableHardwareDevice = true);
    void shutdown();

    void triggerMaterialImpact(uint16_t layerId, float intensity = 1.0f);
    void mixSamples(float* outInterleavedStereo, uint32_t frameCount, uint32_t sampleRate = 48000) noexcept;

private:
    struct Voice {
        std::atomic<bool> active{false};
        uint16_t layerId{0};
        float phase1{0.0f};
        float phase2{0.0f};
        float freq1{440.0f};
        float freq2{880.0f};
        float env{0.0f};
        float decay{0.999f};
        float noiseMix{0.2f};
        float filterState{0.0f};
    };

    static constexpr int kMaxVoices = 8;
    Voice voices_[kMaxVoices]{};
    uint32_t rngState_{0x87654321u};
    bool initialized_{false};
    void* maDevicePtr_{nullptr};
};

} // namespace eatsrealms

#endif // EATSREALMS_AUDIO_FOLEY_HPP
