#pragma once
#include "../graph/IAudioNode.h"
#include <atomic>
#include <algorithm>

namespace isac {

// 立体声增益乘法器（2进2出，受滑块系数控制）
class MultiplyNode : public IAudioNode {
public:
    std::string name() const override { return "Multiply (Stereo Gain)"; }
    int numInputs() const override { return 2; }
    int numOutputs() const override { return 2; }
    std::string inputName(PortIndex p) const override { return p == 0 ? "In L" : "In R"; }
    std::string outputName(PortIndex p) const override { return p == 0 ? "Out L" : "Out R"; }

    void setFactor(double factor) { m_factor.store(factor, std::memory_order_relaxed); }
    double factor() const { return m_factor.load(std::memory_order_relaxed); }

    void process(const AudioBuffer& inBus, AudioBuffer& outBus, int frames) override {
        const AudioSample gain = m_factor.load(std::memory_order_relaxed);
        for (int ch = 0; ch < 2; ++ch) {
            const AudioSample* src = inBus.channel(ch);
            AudioSample* dst = outBus.channel(ch);
            for (int i = 0; i < frames; ++i) {
                dst[i] = src[i] * gain;
            }
        }
    }

private:
    std::atomic<double> m_factor{1.0};
};

} // namespace isac
