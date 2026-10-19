#include <isac/plugin/QtAudioPlugin.h>

#include <QCoreApplication>
#include <QPluginLoader>
#include <cassert>
#include <cmath>
#include <iostream>

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    if (argc < 2) {
        std::cerr << "expected at least one plugin path\n";
        return 2;
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

        auto node = plugin->createNode();
        assert(node);

        isac::AudioBuffer input(2, 160);
        isac::AudioBuffer output(2, 160);
        for (int i = 0; i < input.numFrames(); ++i) {
            input.channel(0)[i] = 1.0;
            input.channel(1)[i] = -1.0;
        }
        node->process(input, output, input.numFrames());

        for (int channel = 0; channel < output.numChannels(); ++channel) {
            for (int frame = 0; frame < output.numFrames(); ++frame) {
                assert(std::isfinite(output.channel(channel)[frame]));
            }
        }
    }

    return 0;
}
