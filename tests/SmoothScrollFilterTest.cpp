#include "ui/widgets/SmoothScrollFilter.h"

#include <QApplication>
#include <QElapsedTimer>
#include <QScrollArea>
#include <QScrollBar>
#include <QThread>
#include <QWheelEvent>
#include <QWidget>

#include <cstdlib>
#include <iostream>

namespace {

void processEventsFor(QApplication& app, int durationMs)
{
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < durationMs) {
        app.processEvents();
        QThread::msleep(1);
    }
}

bool sendWheelEvent(QWidget* receiver, const QPoint& pixelDelta,
                    const QPoint& angleDelta)
{
    const QPoint localPosition(20, 20);
    const QPointF local(localPosition);
    const QPointF global(receiver->mapToGlobal(localPosition));
    QWheelEvent event(local, global, pixelDelta, angleDelta,
                      Qt::NoButton, Qt::NoModifier,
                      Qt::NoScrollPhase, false);
    return QApplication::sendEvent(receiver, &event);
}

} // namespace

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    isac::ui::SmoothScrollFilter filter;
    filter.setSensitivity(1.0);
    filter.setAnimationDurationMs(140);
    app.installEventFilter(&filter);

    QScrollArea area;
    auto* content = new QWidget;
    content->setFixedSize(320, 2400);
    area.setWidget(content);
    area.resize(320, 360);
    area.show();
    processEventsFor(app, 20);

    QScrollBar* scrollBar = area.verticalScrollBar();
    if (scrollBar->maximum() <= 0) {
        std::cerr << "Scroll area did not create a scroll range\n";
        return EXIT_FAILURE;
    }

    sendWheelEvent(area.viewport(), {}, QPoint(0, -120));
    processEventsFor(app, 16);
    const int angleIntermediate = scrollBar->value();
    processEventsFor(app, 350);
    const int angleFinal = scrollBar->value();
    if (angleIntermediate <= 0 || angleIntermediate >= angleFinal ||
        angleFinal < 115 || angleFinal > 125) {
        std::cerr << "Angle wheel motion failed: intermediate="
                  << angleIntermediate << ", final=" << angleFinal << '\n';
        return EXIT_FAILURE;
    }

    scrollBar->setValue(0);
    sendWheelEvent(area.viewport(), QPoint(0, -20), {});
    processEventsFor(app, 16);
    const int pixelIntermediate = scrollBar->value();
    processEventsFor(app, 350);
    const int pixelFinal = scrollBar->value();
    if (pixelIntermediate <= 0 || pixelIntermediate >= pixelFinal ||
        pixelFinal < 23 || pixelFinal > 27) {
        std::cerr << "Pixel wheel motion failed: intermediate="
                  << pixelIntermediate << ", final=" << pixelFinal << '\n';
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
