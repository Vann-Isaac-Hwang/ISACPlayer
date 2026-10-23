#pragma once

#include <isac/plugin/IAudioNode.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <string_view>

namespace isac::plugins {

class MonoGainNode final : public IAudioNode {
public:
    std::string name() const override { return "Mono Gain"; }
    int numInputs() const override { return 1; }
    int numOutputs() const override { return 1; }

    std::string inputName(PortIndex) const override { return "In"; }
    std::string outputName(PortIndex) const override { return "Out"; }

    bool setParameter(std::string_view name, double value) noexcept override
    {
        if (name != "factor") {
            return false;
        }
        setFactor(value);
        return true;
    }

    void setFactor(double factor) noexcept
    {
        if (!std::isfinite(factor)) {
            return;
        }
        m_factor.store(std::clamp(factor, -4.0, 4.0),
                       std::memory_order_relaxed);
    }

    double factor() const noexcept
    {
        return m_factor.load(std::memory_order_relaxed);
    }

    void process(const AudioBuffer& inBus, AudioBuffer& outBus,
                 int frames) override
    {
        const int count = std::clamp(
            frames, 0, std::min(inBus.numFrames(), outBus.numFrames()));
        const double factorValue =
            m_factor.load(std::memory_order_relaxed);
        const AudioSample* input = inBus.channel(0);
        AudioSample* output = outBus.channel(0);

        for (int frame = 0; frame < count; ++frame) {
            output[frame] = input[frame] * factorValue;
        }
    }

private:
    std::atomic<double> m_factor{1.0};
};

static_assert(std::atomic<double>::is_always_lock_free,
              "Mono Gain parameters must use lock-free atomics");

} // namespace isac::plugins
