#pragma once
#include "../graph/IAudioNode.h"

namespace isac {

// 基本加法器：将 Port 0 (In A) 和 Port 1 (In B) 的波形逐采样点相加，从 Port 0 (Out) 输出
class AddNode : public IAudioNode {
public:
    std::string name() const override { return "Adder (A + B)"; }
    int numInputs() const override { return 2; }
    int numOutputs() const override { return 1; }
    std::string inputName(PortIndex p) const override { return p == 0 ? "In A" : "In B"; }
    std::string outputName(PortIndex) const override { return "Sum Out"; }

    void process(const AudioBuffer& inBus, AudioBuffer& outBus, int frames) override {
        const AudioSample* inA = inBus.channel(0);
        const AudioSample* inB = inBus.channel(1);
        AudioSample* out = outBus.channel(0);

        for (int i = 0; i < frames; ++i) {
            out[i] = inA[i] + inB[i];
        }
    }
};

} // namespace isac
