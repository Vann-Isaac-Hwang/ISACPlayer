#pragma once

#include <isac/plugin/IAudioNode.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <string_view>

namespace isac::plugins {

class StereoBalanceNode final : public IAudioNode {
public:
    static constexpr double kMinimumBalance = -1.0;
    static constexpr double kMaximumBalance = 1.0;

    std::string name() const override { return "Stereo Balance"; }
    int numInputs() const override { return 2; }
    int numOutputs() const override { return 2; }

    std::string inputName(PortIndex port) const override
    {
        return port == 0 ? "In L" : "In R";
    }

    std::string outputName(PortIndex port) const override
    {
        return port == 0 ? "Out L" : "Out R";
    }

    bool setParameter(std::string_view name, double value) noexcept override
    {
        if (name != "balance") {
            return false;
        }
        setBalance(value);
        return true;
    }

    void setBalance(double balance) noexcept
    {
        if (!std::isfinite(balance)) {
            return;
        }
        m_balance.store(
            std::clamp(balance, kMinimumBalance, kMaximumBalance),
            std::memory_order_relaxed);
    }

    double balance() const noexcept
    {
        return m_balance.load(std::memory_order_relaxed);
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

        const double balanceValue =
            m_balance.load(std::memory_order_relaxed);
        const double leftGain = balanceValue > 0.0
            ? 1.0 - balanceValue
            : 1.0;
        const double rightGain = balanceValue < 0.0
            ? 1.0 + balanceValue
            : 1.0;

        const AudioSample* inputLeft = inBus.channel(0);
        const AudioSample* inputRight = inBus.channel(1);
        AudioSample* outputLeft = outBus.channel(0);
        AudioSample* outputRight = outBus.channel(1);
        for (int frame = 0; frame < count; ++frame) {
            outputLeft[frame] = inputLeft[frame] * leftGain;
            outputRight[frame] = inputRight[frame] * rightGain;
        }
    }

private:
    std::atomic<double> m_balance{0.0};
};

static_assert(std::atomic<double>::is_always_lock_free,
              "Stereo Balance parameters must use lock-free atomics");

} // namespace isac::plugins
