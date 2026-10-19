#pragma once

#include <isac/plugin/IAudioNode.h>
#include <algorithm>
#include <atomic>
#include <string_view>

namespace isac::plugins {

class GainNode final : public IAudioNode {
public:
    std::string name() const override { return "Stereo Gain"; }
    int numInputs() const override { return 2; }
    int numOutputs() const override { return 2; }

    std::string inputName(PortIndex port) const override {
        return port == 0 ? "In L" : "In R";
    }

    std::string outputName(PortIndex port) const override {
        return port == 0 ? "Out L" : "Out R";
    }

    bool setParameter(std::string_view name, double value) noexcept override {
        if (name != "factor") {
            return false;
        }
        setFactor(value);
        return true;
    }

    void setFactor(double factor) noexcept {
        m_factor.store(std::clamp(factor, 0.0, 4.0), std::memory_order_relaxed);
    }

    double factor() const noexcept {
        return m_factor.load(std::memory_order_relaxed);
    }

    void process(const AudioBuffer& inBus, AudioBuffer& outBus, int frames) override {
        const int count = std::clamp(
            frames, 0, std::min(inBus.numFrames(), outBus.numFrames()));
        const double factorValue = m_factor.load(std::memory_order_relaxed);

        for (int channel = 0; channel < 2; ++channel) {
            const AudioSample* source = inBus.channel(channel);
            AudioSample* destination = outBus.channel(channel);
            for (int frame = 0; frame < count; ++frame) {
                destination[frame] = source[frame] * factorValue;
            }
        }
    }

private:
    std::atomic<double> m_factor{1.0};
};

static_assert(std::atomic<double>::is_always_lock_free,
              "Gain parameters must use lock-free atomics");

} // namespace isac::plugins
