#include "engine/nodes/MultiplyNode.h"
#include "ui/patchbay/PatchbayView.h"

#include <QApplication>

#include <cstdlib>
#include <iostream>
#include <memory>

int main(int argc, char* argv[])
{
    QApplication application(argc, argv);
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
