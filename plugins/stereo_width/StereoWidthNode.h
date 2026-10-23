#pragma once

#include <isac/plugin/IAudioNode.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <string_view>

namespace isac::plugins {

class StereoWidthNode final : public IAudioNode {
public:
    static constexpr double kMinimumWidth = 0.0;
    static constexpr double kMaximumWidth = 2.0;

    std::string name() const override { return "Stereo Width"; }
    int numInputs() const override { return 2; }
    int numOutputs() const override { return 2; }

    std::string inputName(PortIndex port) const override
    {
        return port == 0 ? "Left" : "Right";
    }

    std::string outputName(PortIndex port) const override
    {
        return port == 0 ? "Left" : "Right";
    }

    bool setParameter(std::string_view name, double value) noexcept override
    {
        if (name != "width") {
            return false;
        }
        setWidth(value);
        return true;
    }

    void setWidth(double width) noexcept
    {
        if (!std::isfinite(width)) {
            return;
        }
        m_width.store(
            std::clamp(width, kMinimumWidth, kMaximumWidth),
            std::memory_order_relaxed);
    }

    double width() const noexcept
    {
        return m_width.load(std::memory_order_relaxed);
    }

    void process(const AudioBuffer& inBus, AudioBuffer& outBus,
                 int frames) override
    {
        const int count = std::clamp(
            frames, 0, std::min(inBus.numFrames(), outBus.numFrames()));
        if (count <= 0 || inBus.numChannels() < 2 ||
            outBus.numChannels() < 2) {
            return;
        }

        const double widthValue = m_width.load(std::memory_order_relaxed);
        const AudioSample* inputLeft = inBus.channel(0);
        const AudioSample* inputRight = inBus.channel(1);
        AudioSample* outputLeft = outBus.channel(0);
        AudioSample* outputRight = outBus.channel(1);

        for (int frame = 0; frame < count; ++frame) {
            const AudioSample mid =
                (inputLeft[frame] + inputRight[frame]) * 0.5;
            const AudioSample side =
                (inputLeft[frame] - inputRight[frame]) * 0.5 * widthValue;
            outputLeft[frame] = mid + side;
            outputRight[frame] = mid - side;
        }
    }

private:
    std::atomic<double> m_width{1.0};
};

static_assert(std::atomic<double>::is_always_lock_free,
              "Stereo Width parameters must use lock-free atomics");

} // namespace isac::plugins
