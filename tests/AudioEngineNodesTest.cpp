#include "engine/graph/AudioBuffer.h"
#include "engine/graph/AudioEngine.h"
#include "engine/graph/AudioTapBuffer.h"
#include "engine/graph/PlaybackFade.h"
#include "engine/graph/SpscAudioRingBuffer.h"
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

void testPlaybackFadeEnvelope()
{
    constexpr std::uint64_t fadeFrames = 100;
    constexpr std::uint64_t totalFrames = 1000;
    expectNear(isac::playbackFadeGain(0, totalFrames, fadeFrames),
               0.0, 0.0, "fade-in must start silent");
    expectNear(isac::playbackFadeGain(50, totalFrames, fadeFrames),
               0.5, 1.0e-12, "fade-in midpoint must be linear");
    expectNear(isac::playbackFadeGain(100, totalFrames, fadeFrames),
               1.0, 0.0, "fade-in must reach unity");
    expectNear(isac::playbackFadeGain(950, totalFrames, fadeFrames),
               0.5, 1.0e-12, "fade-out midpoint must be linear");
    expectNear(isac::playbackFadeGain(totalFrames, totalFrames, fadeFrames),
               0.0, 0.0, "fade-out must end silent");
    expectNear(isac::playbackFadeGain(0, 40, fadeFrames),
               0.0, 0.0, "short tracks must begin silent");
    expectNear(isac::playbackFadeGain(20, 40, fadeFrames),
               0.25, 1.0e-12, "overlapping fades must combine smoothly");
    expectNear(isac::playbackFadeGain(20, 40, 0),
               1.0, 0.0, "zero-duration fade must preserve unity gain");
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

void testLegacyNodeReferenceClearedOnRemoval()
{
    auto gain = std::make_shared<isac::MultiplyNode>();
    isac::AudioEngine engine(gain);
    const isac::NodeId gainId = gain->id();

    expect(engine.gainNode() != nullptr,
           "legacy gain reference must be available while the node exists");
    expect(engine.removeNode(gainId),
           "legacy gain node must be removable during preset replacement");
    expect(engine.gainNode() == nullptr,
           "removing a legacy gain node must clear its engine reference");
}

void testDefaultGraphUsesDirectOutput()
{
    isac::AudioEngine engine;
    expect(engine.nodes().size() == 2,
           "default engine graph must contain only source and sink nodes");
    expect(engine.connections().size() == 2,
           "default engine graph must connect both source channels directly");
    expect(std::all_of(
               engine.connections().cbegin(), engine.connections().cend(),
               [&engine](const isac::Connection& connection) {
                   return connection.srcNode == engine.sourceNodeId() &&
                          connection.dstNode == engine.sinkNodeId() &&
                          connection.srcPort == connection.dstPort;
               }),
           "default graph must connect source channels directly to the sink");
}

void testOutputDeviceEnumeration()
{
    isac::AudioEngine engine;
    const auto devices = engine.availableOutputDevices();

    expect(!devices.empty(), "output device list must include system default");
    expect(devices.front().useSystemDefault,
           "system default must be the first output option");
    expect(devices.front().isDefault,
           "system default option must be marked as default");

    std::string error;
    expect(engine.selectOutputDevice(devices.front(), error),
           "selecting system default before playback must succeed");
    expect(!engine.selectedOutputDevice().has_value(),
           "system default selection must clear an explicit device id");
}

void testMasterGainControl()
{
    isac::AudioEngine engine;
    expectNear(engine.masterGain(), 1.0, 0.0,
               "master gain must default to unity");

    engine.setMasterGain(1.75);
    expectNear(engine.masterGain(), 1.75, 0.0,
               "master gain must accept a valid factor");

    engine.setMasterGain(-2.0);
    expectNear(engine.masterGain(), 0.0, 0.0,
               "master gain must clamp negative factors to silence");

    engine.setMasterGain(9.0);
    expectNear(engine.masterGain(), 4.0, 0.0,
               "master gain must clamp to the engine maximum");

    engine.setMasterGain(std::numeric_limits<double>::quiet_NaN());
    expectNear(engine.masterGain(), 4.0, 0.0,
               "invalid master gain must leave the previous value unchanged");
}

void testNativeRateGraphBuffers()
{
    isac::AudioEngine engine;
    for (const std::uint32_t sampleRate :
         {44100u, 96000u, 192000u, 768000u}) {
        const auto plan = isac::GraphCompiler::compile(
            engine.nodes(), engine.connections(), engine.sinkNodeId(),
            128, sampleRate);
        expect(plan != nullptr, "graph must compile at supported source rates");
        for (const auto& step : plan->steps) {
            expect(step.inBus.sampleRate() == sampleRate &&
                       step.outBus.sampleRate() == sampleRate,
                   "compiled DSP buses must retain the requested native rate");
        }
    }
}

void testPlaybackFailureReportsReason()
{
    isac::AudioEngine engine;
    std::string error;
    expect(!engine.loadAndPlay(
               "/path/that/does/not/exist/isacplayer-test.wav", &error),
           "loading a missing file must fail");
    expect(!error.empty(),
           "playback failure must provide a diagnostic");
}

void testSpscAudioRingBuffer()
{
    isac::SpscAudioRingBuffer ring(4, 2);
    const float firstBlock[] = {
        1.0f, 10.0f,
        2.0f, 20.0f,
        3.0f, 30.0f
    };
    expect(ring.write(firstBlock, 3) == 3,
           "ring buffer must accept available producer frames");

    double left[4]{};
    double right[4]{};
    double* channels[] = {left, right};
    expect(ring.readPlanar(channels, 2) == 2,
           "ring buffer must return requested readable frames");
    expectNear(left[0], 1.0, 0.0, "ring buffer left channel must preserve order");
    expectNear(left[1], 2.0, 0.0, "ring buffer left channel must preserve order");
    expectNear(right[0], 10.0, 0.0, "ring buffer right channel must preserve order");
    expectNear(right[1], 20.0, 0.0, "ring buffer right channel must preserve order");

    const float secondBlock[] = {
        4.0f, 40.0f,
        5.0f, 50.0f,
        6.0f, 60.0f
    };
    expect(ring.write(secondBlock, 3) == 3,
           "ring buffer must wrap at the physical end");
    expect(ring.readPlanar(channels, 4) == 4,
           "ring buffer must read wrapped frames in order");
    expectNear(left[0], 3.0, 0.0, "wrapped left channel frame 1");
    expectNear(left[1], 4.0, 0.0, "wrapped left channel frame 2");
    expectNear(left[2], 5.0, 0.0, "wrapped left channel frame 3");
    expectNear(left[3], 6.0, 0.0, "wrapped left channel frame 4");
    expectNear(right[0], 30.0, 0.0, "wrapped right channel frame 1");
    expectNear(right[3], 60.0, 0.0, "wrapped right channel frame 4");
}

void testAudioTapBufferLatestWindow()
{
    isac::AudioTapBuffer tap(5, 2);
    const double leftFirst[] = {1.0, 2.0, 3.0, 4.0};
    const double rightFirst[] = {10.0, 20.0, 30.0, 40.0};
    const double* firstBlock[] = {leftFirst, rightFirst};
    tap.write(firstBlock, 4);

    double leftRead[4]{};
    double rightRead[4]{};
    double* output[] = {leftRead, rightRead};
    expect(tap.readLatest(output, 2) == 2,
           "audio tap must return only the requested latest window");
    expectNear(leftRead[0], 3.0, 0.0, "audio tap latest left sample");
    expectNear(rightRead[1], 40.0, 0.0, "audio tap latest right sample");

    const double leftSecond[] = {5.0, 6.0, 7.0, 8.0};
    const double rightSecond[] = {50.0, 60.0, 70.0, 80.0};
    const double* secondBlock[] = {leftSecond, rightSecond};
    tap.write(secondBlock, 4);
    expect(tap.readLatest(output, 4) == 4,
           "audio tap must read across the ring boundary");
    expectNear(leftRead[0], 5.0, 0.0, "audio tap wrapped left sample");
    expectNear(leftRead[3], 8.0, 0.0, "audio tap wrapped left tail");
    expectNear(rightRead[0], 50.0, 0.0, "audio tap wrapped right sample");
    expectNear(rightRead[3], 80.0, 0.0, "audio tap wrapped right tail");
}

} // namespace

int main()
{
    testDoublePlanarBus();
    testPlaybackFadeEnvelope();
    testBiquadBypass();
    testBiquadPeak();
    testAtomicParameterUpdate();
    testNodeRemoval();
    testLegacyNodeReferenceClearedOnRemoval();
    testDefaultGraphUsesDirectOutput();
    testOutputDeviceEnumeration();
    testMasterGainControl();
    testNativeRateGraphBuffers();
    testPlaybackFailureReportsReason();
    testSpscAudioRingBuffer();
    testAudioTapBufferLatestWindow();
    std::cout << "isac_engine_tests: all tests passed\n";
    return EXIT_SUCCESS;
}
