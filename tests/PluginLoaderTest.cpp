#include <isac/plugin/QtAudioPlugin.h>
#include "ReplayGainNode.h"

#include <QCoreApplication>
#include <QPluginLoader>
#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <iostream>
#include <limits>

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    if (argc < 2) {
        std::cerr << "expected at least one plugin path\n";
        return 2;
    }

    {
        constexpr int totalFrames = 160;
        isac::plugins::ReplayGainNode node;
        isac::AudioBuffer input(2, 80, 48000);
        isac::AudioBuffer output(2, 80, 48000);
        input.channel(0)[0] = 1.0;
        input.channel(1)[0] = 1.0;
        std::array<double, totalFrames> rendered{};
        int offset = 0;
        for (const int blockSize : {23, 41, 17, 79}) {
            input.clear();
            if (offset == 0) {
                input.channel(0)[0] = 1.0;
                input.channel(1)[0] = 1.0;
            }
            node.process(input, output, blockSize);
            for (int frame = 0; frame < blockSize; ++frame) {
                rendered[static_cast<std::size_t>(offset + frame)] =
                    output.channel(0)[frame];
            }
            offset += blockSize;
        }
        assert(offset == totalFrames);
        for (int frame = 0; frame < 64; ++frame) {
            assert(rendered[static_cast<std::size_t>(frame)] == 0.0);
        }
        assert(std::abs(rendered[64] - 1.0) < 1.0e-12);
        assert(std::abs(rendered[64 + 1]) < 1.0e-12);
        assert(node.setParameter("gainDb", -6.0));
        assert(!node.setParameter("missing", 0.0));
        node.setGainDb(std::numeric_limits<double>::quiet_NaN());
        assert(std::isfinite(node.gainDb()));
    }

    for (int argument = 1; argument < argc; ++argument) {
        QPluginLoader loader(QString::fromLocal8Bit(argv[argument]));
        QObject* instance = loader.instance();
        if (!instance) {
            std::cerr << loader.errorString().toStdString() << '\n';
            return 3;
        }

        auto* plugin = qobject_cast<isac::IAudioPlugin*>(instance);
        if (!plugin) {
            std::cerr << "plugin does not implement IAudioPlugin\n";
            return 4;
        }

        auto* compatibility =
            qobject_cast<isac::IAudioPluginCompatibility*>(instance);
        if (!compatibility ||
            compatibility->pluginApiVersion() !=
                ISAC_AUDIO_PLUGIN_API_VERSION) {
            std::cerr << "plugin API version mismatch\n";
            return 5;
        }

        if (auto* parameters =
                qobject_cast<isac::IAudioPluginParameters*>(instance)) {
            for (std::size_t index = 0;
                 index < parameters->parameterCount(); ++index) {
                const auto* descriptor = parameters->parameterAt(index);
                assert(descriptor && descriptor->id &&
                       descriptor->displayName);
                assert(std::isfinite(descriptor->minimum));
                assert(std::isfinite(descriptor->maximum));
                assert(std::isfinite(descriptor->defaultValue));
                assert(descriptor->minimum <= descriptor->defaultValue);
                assert(descriptor->defaultValue <= descriptor->maximum);
            }
        }

        auto node = plugin->createNode();
        assert(node);

        isac::AudioBuffer input(2, 160);
        isac::AudioBuffer output(
            std::max(1, node->numOutputs()), 160);
        for (int i = 0; i < input.numFrames(); ++i) {
            input.channel(0)[i] = 0.75;
            input.channel(1)[i] = 0.75;
        }
        node->process(input, output, input.numFrames());

        for (int channel = 0; channel < output.numChannels(); ++channel) {
            for (int frame = 0; frame < output.numFrames(); ++frame) {
                assert(std::isfinite(output.channel(channel)[frame]));
            }
        }

        if (QString::fromLatin1(plugin->pluginId()) ==
            QStringLiteral("isac.add")) {
            for (int frame = 0; frame < output.numFrames(); ++frame) {
                assert(std::abs(output.channel(0)[frame] - 1.5) < 1.0e-12);
            }
        }

        if (QString::fromLatin1(plugin->pluginId()) ==
            QStringLiteral("isac.mono_gain")) {
            assert(node->numInputs() == 1);
            assert(node->numOutputs() == 1);
            assert(node->setParameter("factor", 2.0));
            node->process(input, output, input.numFrames());
            for (int frame = 0; frame < output.numFrames(); ++frame) {
                assert(std::abs(output.channel(0)[frame] - 1.5) < 1.0e-12);
            }

            assert(node->setParameter("factor", -1.0));
            node->process(input, output, input.numFrames());
            for (int frame = 0; frame < output.numFrames(); ++frame) {
                assert(std::abs(output.channel(0)[frame] + 0.75) < 1.0e-12);
            }

            assert(!node->setParameter("missing", 0.0));
        }

        if (QString::fromLatin1(plugin->pluginId()) ==
            QStringLiteral("isac.mono_delay")) {
            assert(node->numInputs() == 1);
            assert(node->numOutputs() == 1);

            auto zeroDelayNode = plugin->createNode();
            auto secondZeroDelayNode = plugin->createNode();
            assert(zeroDelayNode && secondZeroDelayNode);
            isac::AudioBuffer zeroInput(1, 80, 48000);
            isac::AudioBuffer zeroOutput(1, 80, 48000);
            isac::AudioBuffer secondZeroOutput(1, 80, 48000);
            for (int frame = 0; frame < zeroInput.numFrames(); ++frame) {
                zeroInput.channel(0)[frame] =
                    std::sin(0.17 * static_cast<double>(frame));
            }
            zeroDelayNode->process(
                zeroInput, zeroOutput, zeroInput.numFrames());
            secondZeroDelayNode->process(
                zeroInput, secondZeroOutput, zeroInput.numFrames());
            for (int frame = 0; frame < zeroInput.numFrames(); ++frame) {
                assert(std::abs(zeroOutput.channel(0)[frame] -
                                zeroInput.channel(0)[frame]) < 1.0e-12);
                assert(std::abs(zeroOutput.channel(0)[frame] -
                                secondZeroOutput.channel(0)[frame]) <
                       1.0e-12);
            }

            auto delayedNode = plugin->createNode();
            assert(delayedNode);
            assert(delayedNode->setParameter("delayMs", 1.0));
            assert(!delayedNode->setParameter("missing", 0.0));

            isac::AudioBuffer delayInput(1, 80, 48000);
            isac::AudioBuffer delayOutput(1, 80, 48000);
            delayInput.channel(0)[0] = 1.0;
            delayedNode->process(
                delayInput, delayOutput, delayInput.numFrames());
            for (int frame = 0; frame < 48; ++frame) {
                assert(std::abs(delayOutput.channel(0)[frame]) < 1.0e-12);
            }
            assert(std::abs(delayOutput.channel(0)[48] - 1.0) < 1.0e-12);

            auto firstFractionalNode = plugin->createNode();
            auto secondFractionalNode = plugin->createNode();
            assert(firstFractionalNode && secondFractionalNode);
            assert(firstFractionalNode->setParameter("delayMs", 0.1));
            assert(secondFractionalNode->setParameter("delayMs", 0.1));
            isac::AudioBuffer fractionalInput(1, 160, 48000);
            isac::AudioBuffer firstFractionalOutput(1, 160, 48000);
            isac::AudioBuffer secondFractionalOutput(1, 160, 48000);
            fractionalInput.channel(0)[0] = 1.0;
            firstFractionalNode->process(
                fractionalInput, firstFractionalOutput,
                fractionalInput.numFrames());
            secondFractionalNode->process(
                fractionalInput, secondFractionalOutput,
                fractionalInput.numFrames());
            for (int frame = 0; frame < fractionalInput.numFrames(); ++frame) {
                assert(std::abs(firstFractionalOutput.channel(0)[frame] -
                                secondFractionalOutput.channel(0)[frame]) <
                       1.0e-12);
            }
        }

        if (QString::fromLatin1(plugin->pluginId()) ==
            QStringLiteral("isac.stereo_width")) {
            assert(node->numInputs() == 2);
            assert(node->numOutputs() == 2);
            assert(node->setParameter("width", 0.0));

            isac::AudioBuffer widthInput(2, 3);
            isac::AudioBuffer widthOutput(2, 3);
            widthInput.channel(0)[0] = 1.0;
            widthInput.channel(1)[0] = -1.0;
            widthInput.channel(0)[1] = 1.0;
            widthInput.channel(1)[1] = 1.0;
            node->process(widthInput, widthOutput, 3);
            assert(std::abs(widthOutput.channel(0)[0]) < 1.0e-12);
            assert(std::abs(widthOutput.channel(1)[0]) < 1.0e-12);
            assert(std::abs(widthOutput.channel(0)[1] - 1.0) < 1.0e-12);
            assert(std::abs(widthOutput.channel(1)[1] - 1.0) < 1.0e-12);

            assert(node->setParameter("width", 2.0));
            node->process(widthInput, widthOutput, 3);
            assert(std::abs(widthOutput.channel(0)[0] - 2.0) < 1.0e-12);
            assert(std::abs(widthOutput.channel(1)[0] + 2.0) < 1.0e-12);
            assert(!node->setParameter("missing", 0.0));
        }
    }

    return 0;
}
