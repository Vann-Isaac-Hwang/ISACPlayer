#include "engine/nodes/MultiplyNode.h"
#include "ui/patchbay/DspPresetCatalog.h"
#include "ui/patchbay/PatchbayView.h"

#include <QApplication>
#include <QFile>
#include <QTemporaryDir>

#include <cstdlib>
#include <iostream>
#include <memory>

int main(int argc, char* argv[])
{
    QApplication application(argc, argv);
    {
        QTemporaryDir presetDirectory;
        if (!presetDirectory.isValid()) {
            std::cerr << "FAIL: temporary preset directory must be available\n";
            return EXIT_FAILURE;
        }
        QFile presetFile(presetDirectory.filePath(QStringLiteral("test.json")));
        if (!presetFile.open(QIODevice::WriteOnly) ||
            presetFile.write(R"({
                "version": 1,
                "id": "test",
                "name": "Test Route",
                "connections": [
                    {"from":"source","output":0,"to":"gain","input":0},
                    {"from":"source","output":1,"to":"gain","input":1},
                    {"from":"gain","output":0,"to":"sink","input":0},
                    {"from":"gain","output":1,"to":"sink","input":1}
                ]
            })") < 0) {
            std::cerr << "FAIL: preset fixture must be written\n";
            return EXIT_FAILURE;
        }
        presetFile.close();

        QString loadError;
        const auto presets = isac::ui::DspPresetCatalog::load(
            presetDirectory.path(), &loadError);
        if (presets.size() != 1 || !loadError.isEmpty()) {
            std::cerr << "FAIL: valid JSON preset must load\n";
            return EXIT_FAILURE;
        }

        isac::AudioEngine presetEngine;
        const auto gain = std::make_shared<isac::MultiplyNode>();
        const auto gainId = presetEngine.addNode(gain);
        QHash<QString, isac::NodeId> roles{
            {QStringLiteral("source"), presetEngine.sourceNodeId()},
            {QStringLiteral("gain"), gainId},
            {QStringLiteral("sink"), presetEngine.sinkNodeId()}
        };
        QString applyError;
        if (!isac::ui::DspPresetCatalog::apply(
                presets.front(), presetEngine, roles, &applyError) ||
            presetEngine.connections().size() != 4) {
            std::cerr << "FAIL: valid preset must replace graph connections\n";
            return EXIT_FAILURE;
        }

        auto invalidPreset = presets.front();
        invalidPreset.connections[0].destinationPort = 9;
        const auto previousConnections = presetEngine.connections();
        if (isac::ui::DspPresetCatalog::apply(
                invalidPreset, presetEngine, roles, &applyError) ||
            presetEngine.connections().size() != previousConnections.size()) {
            std::cerr << "FAIL: invalid preset must preserve current connections\n";
            return EXIT_FAILURE;
        }

        isac::ui::DspPreset customPreset;
        customPreset.version = 2;
        customPreset.name = QStringLiteral("Custom Route");
        customPreset.nodes = {
            {QStringLiteral("source"), {}, QPointF(-300.0, -40.0)},
            {QStringLiteral("sink"), {}, QPointF(300.0, -40.0)},
            {QStringLiteral("test.gain#0"), QStringLiteral("test.gain"),
             QPointF(0.0, 100.0)}
        };
        customPreset.connections = {
            {QStringLiteral("source"), 0, QStringLiteral("test.gain#0"), 0},
            {QStringLiteral("source"), 1, QStringLiteral("test.gain#0"), 1},
            {QStringLiteral("test.gain#0"), 0, QStringLiteral("sink"), 0},
            {QStringLiteral("test.gain#0"), 1, QStringLiteral("sink"), 1}
        };
        QString catalogError;
        if (!isac::ui::DspPresetCatalog::save(
                presetDirectory.path(), customPreset, &catalogError) ||
            customPreset.id.isEmpty()) {
            std::cerr << "FAIL: custom preset must be saved atomically\n";
            return EXIT_FAILURE;
        }

        auto reloaded = isac::ui::DspPresetCatalog::load(
            presetDirectory.path(), &loadError);
        const auto savedPreset = std::find_if(
            reloaded.cbegin(), reloaded.cend(),
            [&customPreset](const isac::ui::DspPreset& preset) {
                return preset.id == customPreset.id;
            });
        if (savedPreset == reloaded.cend() || savedPreset->version != 2 ||
            savedPreset->nodes != customPreset.nodes ||
            savedPreset->connections.size() != 4) {
            std::cerr << "FAIL: custom topology and node positions must round-trip\n";
            return EXIT_FAILURE;
        }

        if (!isac::ui::DspPresetCatalog::rename(
                presetDirectory.path(), customPreset.id,
                QStringLiteral("Renamed Route"), &catalogError)) {
            std::cerr << "FAIL: custom preset must be renameable\n";
            return EXIT_FAILURE;
        }
        reloaded = isac::ui::DspPresetCatalog::load(
            presetDirectory.path(), &loadError);
        const auto renamedPreset = std::find_if(
            reloaded.cbegin(), reloaded.cend(),
            [&customPreset](const isac::ui::DspPreset& preset) {
                return preset.id == customPreset.id;
            });
        if (renamedPreset == reloaded.cend() ||
            renamedPreset->name != QStringLiteral("Renamed Route")) {
            std::cerr << "FAIL: renamed preset must be reloaded from disk\n";
            return EXIT_FAILURE;
        }
        if (isac::ui::DspPresetCatalog::rename(
                presetDirectory.path(), QStringLiteral("stereo"),
                QStringLiteral("Override Built-in"), &catalogError) ||
            isac::ui::DspPresetCatalog::remove(
                presetDirectory.path(), QStringLiteral("stereo"),
                &catalogError)) {
            std::cerr << "FAIL: built-in presets must remain read-only\n";
            return EXIT_FAILURE;
        }
        if (!isac::ui::DspPresetCatalog::remove(
                presetDirectory.path(), customPreset.id, &catalogError)) {
            std::cerr << "FAIL: custom preset must be deletable\n";
            return EXIT_FAILURE;
        }
    }

    isac::AudioEngine engine;
    const auto processor = std::make_shared<isac::MultiplyNode>();
    const auto processorId = engine.addNode(processor);
    const auto sourceId = engine.sourceNodeId();
    const auto sinkId = engine.sinkNodeId();
    isac::ui::PatchbayScene scene(engine);

    for (int iteration = 0; iteration < 500; ++iteration) {
        engine.clearConnections();
        engine.connect(sourceId, 0, processorId, 0);
        engine.connect(sourceId, 1, processorId, 1);
        engine.connect(processorId, iteration % 2, sinkId, 0);
        engine.connect(processorId, 1 - iteration % 2, sinkId, 1);
        if (!engine.rebuildGraph()) {
            std::cerr << "FAIL: valid preset topology must compile\n";
            return EXIT_FAILURE;
        }
        scene.syncFromEngine();
    }

    std::cout << "isac_patchbay_tests: repeated scene rebuilds passed\n";
    return EXIT_SUCCESS;
}
