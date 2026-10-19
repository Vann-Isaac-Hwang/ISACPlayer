#pragma once

#include <isac/plugin/IAudioNode.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>

namespace isac::plugins {

class ReplayGainNode final : public IAudioNode {
public:
    std::string name() const override { return "ReplayGain (Stereo)"; }
    int numInputs() const override { return 2; }
    int numOutputs() const override { return 2; }
    std::string inputName(PortIndex port) const override {
        return port == 0 ? "In L" : "In R";
    }
    std::string outputName(PortIndex port) const override {
        return port == 0 ? "Out L" : "Out R";
    }

    // The host can wire parsed track metadata to this atomic parameter later.
    void setGainDb(double gainDb) {
        m_targetGain.store(std::clamp(gainDb, -24.0, 12.0), std::memory_order_relaxed);
    }

    double gainDb() const {
        return m_targetGain.load(std::memory_order_relaxed);
    }

    void process(const AudioBuffer& inBus, AudioBuffer& outBus, int frames) override {
        const int count = std::clamp(frames, 0, outBus.numFrames());
        const double target = dbToLinear(m_targetGain.load(std::memory_order_relaxed));
        const double step = (target - m_currentGain) / static_cast<double>(std::max(1, count));

        for (int ch = 0; ch < 2; ++ch) {
            const AudioSample* input = inBus.channel(ch);
            AudioSample* output = outBus.channel(ch);
            double gain = m_currentGain;

            for (int i = 0; i < count; ++i) {
                gain += step;
                const int delayIndex = (m_delayIndex + i) % kLookaheadFrames;
                const AudioSample delayed = m_delay[ch][delayIndex];
                m_delay[ch][delayIndex] = input[i];
                output[i] = delayed * gain;
            }
        }

        m_currentGain = target;
        m_delayIndex = (m_delayIndex + count) % kLookaheadFrames;
    }

private:
    static constexpr int kLookaheadFrames = 64;

    static double dbToLinear(double db) {
        return std::pow(10.0, db / 20.0);
    }

    std::atomic<double> m_targetGain{0.0};
    double m_currentGain = 1.0;
    int m_delayIndex = 0;
    std::array<std::array<AudioSample, kLookaheadFrames>, 2> m_delay{};
};

} // namespace isac::plugins
