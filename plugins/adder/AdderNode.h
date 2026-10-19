#pragma once

#include <isac/plugin/IAudioNode.h>
#include <algorithm>
#include <cmath>

namespace isac::plugins {

class AdderNode final : public IAudioNode {
public:
    std::string name() const override { return "Adder (A + B)"; }
    int numInputs() const override { return 2; }
    int numOutputs() const override { return 1; }

    std::string inputName(PortIndex port) const override {
        return port == 0 ? "In A" : "In B";
    }

    std::string outputName(PortIndex) const override { return "Sum Out"; }

    void process(const AudioBuffer& inBus, AudioBuffer& outBus, int frames) override {
        const int count = std::clamp(
            frames, 0, std::min(inBus.numFrames(), outBus.numFrames()));
        const AudioSample* first = inBus.channel(0);
        const AudioSample* second = inBus.channel(1);
        AudioSample* output = outBus.channel(0);

        for (int frame = 0; frame < count; ++frame) {
            output[frame] = std::clamp(first[frame] + second[frame], -1.0, 1.0);
        }
    }
};

} // namespace isac::plugins
