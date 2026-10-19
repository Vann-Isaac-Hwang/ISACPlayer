#include "engine/graph/AudioBuffer.h"
#include "engine/graph/AudioEngine.h"
#include "engine/nodes/BiquadEqualizerNode.h"
#include "engine/nodes/MultiplyNode.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>

namespace {

void expect(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

void expectNear(double actual, double expected, double tolerance, const char* message)
{
    if (std::abs(actual - expected) > tolerance) {
        std::cerr << "FAIL: " << message
                  << " actual=" << actual << " expected=" << expected << '\n';
        std::exit(EXIT_FAILURE);
    }
}

void testDoublePlanarBus()
{
    isac::AudioBuffer64 buffer(2, 4, 48000);
    buffer.channel(0)[0] = 1.0 + 1.0e-12;
    buffer.channel(1)[0] = -2.0;

    expect(sizeof(*buffer.channel(0)) == sizeof(double), "audio bus must use double samples");
    expectNear(buffer.channel(0)[0], 1.0 + 1.0e-12, 1.0e-15,
               "double sample precision must be preserved");
    expect(buffer.channel(1) == buffer.channel(0) + buffer.numFrames(),
           "audio bus must use planar channel storage");
}

void testBiquadBypass()
{
    constexpr int frames = 128;
    isac::AudioBuffer input(2, frames, 48000);
    isac::AudioBuffer output(2, frames, 48000);
    isac::BiquadEqualizerNode eq;
    eq.setBypassed(true);

    for (int channel = 0; channel < 2; ++channel) {
        for (int frame = 0; frame < frames; ++frame) {
            input.channel(channel)[frame] = std::sin(0.13 * frame + channel);
        }
    }

    eq.process(input, output, frames);
    for (int channel = 0; channel < 2; ++channel) {
        for (int frame = 0; frame < frames; ++frame) {
            expectNear(output.channel(channel)[frame], input.channel(channel)[frame], 0.0,
                       "bypass must copy samples exactly");
        }
    }
}

void testBiquadPeak()
{
    constexpr int frames = 512;
    isac::AudioBuffer input(2, frames, 48000);
    isac::AudioBuffer output(2, frames, 48000);
    isac::BiquadEqualizerNode eq;
    eq.setBand(5, isac::BiquadType::Peak, 1000.0, 12.0, 1.0);

    input.channel(0)[0] = 1.0;
    input.channel(1)[0] = 1.0;
    eq.process(input, output, frames);

    bool changed = false;
    for (int channel = 0; channel < 2; ++channel) {
        for (int frame = 0; frame < frames; ++frame) {
            const double value = output.channel(channel)[frame];
            expect(std::isfinite(value), "biquad output must remain finite");
            changed = changed || std::abs(value - input.channel(channel)[frame]) > 1.0e-9;
        }
    }
    expect(changed, "an enabled peak band must affect an impulse");
}

void testAtomicParameterUpdate()
{
    isac::BiquadEqualizerNode eq;
    isac::AudioBuffer input(2, 64, 48000);
    isac::AudioBuffer output(2, 64, 48000);

    for (int iteration = 0; iteration < 100; ++iteration) {
        eq.setBandGainDb(0, iteration % 2 == 0 ? -18.0 : 18.0);
        eq.process(input, output, input.numFrames());
        for (int frame = 0; frame < input.numFrames(); ++frame) {
            expect(std::isfinite(output.channel(0)[frame]),
                   "parameter updates must not produce invalid samples");
        }
    }
}

void testNodeRemoval()
{
    isac::AudioEngine engine;
    const auto node = std::make_shared<isac::MultiplyNode>();
    const isac::NodeId nodeId = engine.addNode(node);
    const isac::NodeId sourceId = engine.sourceNodeId();
    const isac::NodeId sinkId = engine.sinkNodeId();

    expect(engine.hasNode(nodeId), "new node must be visible in the engine");
    engine.connect(sourceId, 0, nodeId, 0);
    engine.connect(nodeId, 0, sinkId, 0);
    expect(engine.removeNode(nodeId), "user-created node must be removable");
    expect(!engine.hasNode(nodeId), "removed node must leave the node registry");
    for (const auto& connection : engine.connections()) {
        expect(connection.srcNode != nodeId && connection.dstNode != nodeId,
               "removing a node must remove all attached connections");
    }
    expect(!engine.removeNode(sourceId), "source endpoint must not be removable");
    expect(!engine.removeNode(sinkId), "sink endpoint must not be removable");
}

} // namespace

int main()
{
    testDoublePlanarBus();
    testBiquadBypass();
    testBiquadPeak();
    testAtomicParameterUpdate();
    testNodeRemoval();
    std::cout << "isac_engine_tests: all tests passed\n";
    return EXIT_SUCCESS;
}
