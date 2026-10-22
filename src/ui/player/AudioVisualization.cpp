#include "AudioVisualization.h"

#include <QMouseEvent>
#include <QPainter>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <fftw3.h>
#include <numbers>

namespace isac::ui {
namespace {

constexpr int kFftSize = 4096;
constexpr int kWaterfallWidth = 512;
constexpr int kWaterfallHeight = 128;
constexpr int kFrequencyBins = kFftSize / 2 + 1;

qreal mouseX(const QMouseEvent* event)
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    return event->position().x();
#else
    return event->localPos().x();
#endif
}

QColor monochromeLevel(double value)
{
    const int level = std::clamp(
        static_cast<int>(std::pow(std::clamp(value, 0.0, 1.0), 0.72) * 235.0),
        12, 235);
    return QColor(level, level, level);
}

} // namespace

WaveformProgressWidget::WaveformProgressWidget(QWidget* parent)
    : QWidget(parent)
{
    setMinimumHeight(34);
    setCursor(Qt::PointingHandCursor);
    setAccessibleName(QStringLiteral("播放进度波形"));
}

void WaveformProgressWidget::setSamples(const double* samples,
                                        std::size_t count)
{
    if (!samples || count == 0) {
        m_samples.clear();
        update();
        return;
    }
    m_samples.assign(samples, samples + count);
    update();
}

void WaveformProgressWidget::setProgress(double progress)
{
    if (m_dragging) {
        return;
    }
    m_progress = std::clamp(progress, 0.0, 1.0);
    update();
}

void WaveformProgressWidget::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, false);
    const QRectF bounds = rect().adjusted(1, 1, -1, -1);
    painter.fillRect(bounds, QColor(30, 32, 40));

    const qreal centerY = bounds.center().y();
    const qreal halfHeight = std::max<qreal>(2.0, bounds.height() * 0.43);
    const int columns = std::max(1, static_cast<int>(bounds.width()));
    const std::size_t sampleCount = m_samples.size();
    const qreal progressX = bounds.left() + bounds.width() * m_progress;

    for (int x = 0; x < columns; ++x) {
        double peak = 0.0;
        if (sampleCount > 0) {
            const std::size_t begin =
                static_cast<std::size_t>(x) * sampleCount / columns;
            const std::size_t end = std::max(
                begin + 1,
                static_cast<std::size_t>(x + 1) * sampleCount / columns);
            for (std::size_t index = begin;
                 index < std::min(end, sampleCount); ++index) {
                peak = std::max(peak, std::abs(m_samples[index]));
            }
        }
        const qreal amplitude = std::clamp(
            static_cast<qreal>(peak), 0.035, 1.0) * halfHeight;
        const qreal xPosition = bounds.left() + x;
        painter.setPen(QPen(xPosition <= progressX
                                ? QColor(235, 237, 240)
                                : QColor(105, 109, 118),
                            1.0));
        painter.drawLine(QPointF(xPosition, centerY - amplitude),
                         QPointF(xPosition, centerY + amplitude));
    }

    painter.setPen(QPen(QColor(255, 255, 255, 128), 4.0));
    painter.drawLine(QPointF(progressX, bounds.top()),
                     QPointF(progressX, bounds.bottom()));
    painter.setPen(QPen(QColor(70, 73, 81), 1.0));
    painter.drawRect(bounds);
}

void WaveformProgressWidget::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        m_dragging = true;
        seekAt(mouseX(event));
        event->accept();
        return;
    }
    QWidget::mousePressEvent(event);
}

void WaveformProgressWidget::mouseMoveEvent(QMouseEvent* event)
{
    if (m_dragging && (event->buttons() & Qt::LeftButton)) {
        seekAt(mouseX(event));
        event->accept();
        return;
    }
    QWidget::mouseMoveEvent(event);
}

void WaveformProgressWidget::mouseReleaseEvent(QMouseEvent* event)
{
    if (m_dragging && event->button() == Qt::LeftButton) {
        seekAt(mouseX(event));
        emit seekRequested(m_progress);
        m_dragging = false;
        event->accept();
        return;
    }
    QWidget::mouseReleaseEvent(event);
}

void WaveformProgressWidget::seekAt(qreal x)
{
    if (width() <= 0) {
        return;
    }
    m_progress = std::clamp(x / width(), 0.0, 1.0);
    update();
}

struct WaterfallWidget::FftState {
    double* input = static_cast<double*>(
        fftw_malloc(sizeof(double) * kFftSize));
    fftw_complex* output = static_cast<fftw_complex*>(
        fftw_malloc(sizeof(fftw_complex) * kFrequencyBins));
    fftw_plan plan = input && output
        ? fftw_plan_dft_r2c_1d(
              kFftSize, input, output, FFTW_ESTIMATE)
        : nullptr;
    std::vector<double> windowCoefficients = std::vector<double>(
        static_cast<std::size_t>(kFftSize), 0.0);
    std::vector<double> bandMagnitudes = std::vector<double>(
        static_cast<std::size_t>(kWaterfallWidth), 0.0);

    FftState()
    {
        for (int index = 0; index < kFftSize; ++index) {
            windowCoefficients[static_cast<std::size_t>(index)] =
                0.5 - 0.5 * std::cos(
                    2.0 * std::numbers::pi * index / (kFftSize - 1));
        }
    }

    ~FftState()
    {
        if (plan) {
            fftw_destroy_plan(plan);
        }
        fftw_free(output);
        fftw_free(input);
    }
};

WaterfallWidget::WaterfallWidget(QWidget* parent)
    : QWidget(parent)
    , m_fft(std::make_unique<FftState>())
    , m_history(kWaterfallWidth, kWaterfallHeight, QImage::Format_RGB32)
{
    m_history.fill(QColor(22, 24, 29));
    setMinimumSize(120, 72);
    setAccessibleName(QStringLiteral("实时频谱瀑布图"));
}

WaterfallWidget::~WaterfallWidget() = default;

void WaterfallWidget::pushSamples(const double* left, const double* right,
                                  std::size_t count,
                                  std::uint32_t sampleRate)
{
    if (!left || !right || count == 0 || !m_fft->plan) {
        return;
    }
    m_sampleRate = std::max<std::uint32_t>(1, sampleRate);
    const std::size_t available = std::min<std::size_t>(count, kFftSize);
    const std::size_t sourceStart = count - available;
    const std::size_t windowStart = kFftSize - available;
    std::fill(m_fft->input, m_fft->input + windowStart, 0.0);
    for (std::size_t index = 0; index < available; ++index) {
        const double mono =
            (left[sourceStart + index] + right[sourceStart + index]) * 0.5;
        m_fft->input[windowStart + index] =
            mono * m_fft->windowCoefficients[windowStart + index];
    }
    fftw_execute(m_fft->plan);

    constexpr double minFrequency = 30.0;
    const double maxFrequency =
        std::max(minFrequency * 1.01, m_sampleRate * 0.5);
    for (int column = 0; column < kWaterfallWidth; ++column) {
        const double normalized =
            static_cast<double>(column) / (kWaterfallWidth - 1);
        const double lowFrequency =
            minFrequency * std::pow(maxFrequency / minFrequency, normalized);
        const double highFrequency =
            minFrequency * std::pow(
                maxFrequency / minFrequency,
                static_cast<double>(column + 1) / (kWaterfallWidth - 1));
        const int firstBin = std::clamp(
            static_cast<int>(lowFrequency * kFftSize / m_sampleRate),
            1, kFrequencyBins - 1);
        const int lastBin = std::clamp(
            static_cast<int>(std::ceil(
                highFrequency * kFftSize / m_sampleRate)),
            firstBin + 1, kFrequencyBins);
        double magnitude = 0.0;
        for (int bin = firstBin; bin < lastBin; ++bin) {
            magnitude = std::max(
                magnitude,
                std::hypot(m_fft->output[bin][0], m_fft->output[bin][1]));
        }
        const double normalizedMagnitude = std::clamp(
            std::log10(1.0 + magnitude * 0.06) / 1.35, 0.0, 1.0);
        m_fft->bandMagnitudes[static_cast<std::size_t>(column)] =
            normalizedMagnitude;
    }

    for (int rowIndex = 0; rowIndex < m_history.height() - 1; ++rowIndex) {
        std::memmove(m_history.scanLine(rowIndex),
                     m_history.constScanLine(rowIndex + 1),
                     static_cast<std::size_t>(m_history.bytesPerLine()));
    }
    QRgb* row = reinterpret_cast<QRgb*>(
        m_history.scanLine(m_history.height() - 1));
    for (int column = 0; column < kWaterfallWidth; ++column) {
        row[column] = monochromeLevel(
                          m_fft->bandMagnitudes[static_cast<std::size_t>(column)])
                          .rgb();
    }
    update();
}

void WaterfallWidget::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.fillRect(rect(), QColor(22, 24, 29));
    painter.setRenderHint(QPainter::SmoothPixmapTransform, false);
    painter.drawImage(rect().adjusted(1, 1, -1, -1), m_history);
    painter.setPen(QPen(QColor(73, 76, 84), 1.0));
    painter.drawRect(rect().adjusted(0, 0, -1, -1));
}

} // namespace isac::ui
