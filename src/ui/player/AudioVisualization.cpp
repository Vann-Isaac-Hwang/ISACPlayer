#include "AudioVisualization.h"
#include "ui/theme/ThemeManager.h"

#include <QMouseEvent>
#include <QPainter>
#include <QFontMetrics>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <fftw3.h>
#include <numbers>

namespace isac::ui {
namespace {

constexpr int kDefaultFftSize = 8192;
constexpr int kMinimumFftSize = 2048;
constexpr int kMaximumFftSize = 16384;
constexpr int kWaterfallWidth = 512;
constexpr int kWaterfallHeight = 128;

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
    const auto& colors = ThemeManager::paletteColors();
    const qreal amount = std::clamp(
        static_cast<qreal>(std::pow(std::clamp(value, 0.0, 1.0), 0.72)),
        0.0, 1.0);
    return QColor(
        std::lerp(colors.visualizationLow.red(),
                  colors.visualizationHigh.red(), amount),
        std::lerp(colors.visualizationLow.green(),
                  colors.visualizationHigh.green(), amount),
        std::lerp(colors.visualizationLow.blue(),
                  colors.visualizationHigh.blue(), amount));
}

} // namespace

WaveformProgressWidget::WaveformProgressWidget(QWidget* parent)
    : QWidget(parent)
{
    setMinimumHeight(34);
    setCursor(Qt::PointingHandCursor);
    setMouseTracking(true);
    setAccessibleName(QStringLiteral("Playback waveform"));
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

void WaveformProgressWidget::setDuration(double seconds)
{
    m_durationSeconds = std::max(0.0, seconds);
    update();
}

void WaveformProgressWidget::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, false);
    const QRectF bounds = rect().adjusted(1, 1, -1, -1);
    const auto& colors = ThemeManager::paletteColors();
    painter.fillRect(bounds, colors.visualizationBackground);

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
                                ? colors.visualizationHigh
                                : colors.visualizationLow,
                            1.0));
        painter.drawLine(QPointF(xPosition, centerY - amplitude),
                         QPointF(xPosition, centerY + amplitude));
    }

    painter.setPen(QPen(colors.textPrimary, 4.0));
    painter.drawLine(QPointF(progressX, bounds.top()),
                     QPointF(progressX, bounds.bottom()));

    if (m_hovered && width() > 0) {
        const qreal hoverX = std::clamp(
            m_hoverX, bounds.left(), bounds.right());
        painter.setPen(QPen(colors.accentHover, 1.0));
        painter.drawLine(QPointF(hoverX, bounds.top()),
                         QPointF(hoverX, bounds.bottom()));
    }

    painter.setPen(QPen(colors.visualizationBorder, 1.0));
    painter.drawRect(bounds);
}

bool WaveformProgressWidget::event(QEvent* event)
{
    if (event->type() == QEvent::Enter) {
        m_hovered = true;
        m_hoverX = width() * m_progress;
        emit hoverPreviewChanged(true, m_progress);
        update();
    } else if (event->type() == QEvent::Leave && !m_dragging) {
        m_hovered = false;
        emit hoverPreviewChanged(false, 0.0);
        update();
    }
    return QWidget::event(event);
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
    m_hoverX = mouseX(event);
    m_hovered = true;
    const qreal progress = width() > 0
        ? std::clamp(m_hoverX / width(), 0.0, 1.0)
        : 0.0;
    emit hoverPreviewChanged(true, progress);
    update();
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

QString WaveformProgressWidget::formatTime(double seconds)
{
    const int totalSeconds = std::max(0, qRound(seconds));
    const int hours = totalSeconds / 3600;
    const int minutes = (totalSeconds % 3600) / 60;
    const int remainingSeconds = totalSeconds % 60;
    if (hours > 0) {
        return QStringLiteral("%1:%2:%3")
            .arg(hours)
            .arg(minutes, 2, 10, QLatin1Char('0'))
            .arg(remainingSeconds, 2, 10, QLatin1Char('0'));
    }
    return QStringLiteral("%1:%2")
        .arg(minutes)
        .arg(remainingSeconds, 2, 10, QLatin1Char('0'));
}

struct WaterfallWidget::FftState {
    int fftSize = 0;
    int frequencyBins = 0;
    double* input = nullptr;
    fftw_complex* output = nullptr;
    fftw_plan plan = nullptr;
    std::vector<double> windowCoefficients;
    std::vector<double> bandMagnitudes = std::vector<double>(
        static_cast<std::size_t>(kWaterfallWidth), 0.0);

    explicit FftState(int requestedFftSize)
        : fftSize(requestedFftSize)
        , frequencyBins(requestedFftSize / 2 + 1)
        , input(static_cast<double*>(
              fftw_malloc(sizeof(double) *
                          static_cast<std::size_t>(requestedFftSize))))
        , output(static_cast<fftw_complex*>(
              fftw_malloc(sizeof(fftw_complex) *
                          static_cast<std::size_t>(frequencyBins))))
        , plan(input && output
                   ? fftw_plan_dft_r2c_1d(
                         fftSize, input, output, FFTW_ESTIMATE)
                   : nullptr)
        , windowCoefficients(
              static_cast<std::size_t>(requestedFftSize), 0.0)
    {
        for (int index = 0; index < fftSize; ++index) {
            windowCoefficients[static_cast<std::size_t>(index)] =
                0.5 - 0.5 * std::cos(
                    2.0 * std::numbers::pi * index / (fftSize - 1));
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
    , m_fft(std::make_unique<FftState>(kDefaultFftSize))
    , m_history(kWaterfallWidth, kWaterfallHeight, QImage::Format_RGB32)
{
    m_history.fill(ThemeManager::paletteColors().visualizationBackground);
    setMinimumSize(120, 72);
    setAccessibleName(QStringLiteral("Live spectrum waterfall"));
    if (auto* themeManager = ThemeManager::instance()) {
        connect(themeManager, &ThemeManager::themeChanged, this, [this]() {
            m_history.fill(
                ThemeManager::paletteColors().visualizationBackground);
            update();
        });
    }
}

WaterfallWidget::~WaterfallWidget() = default;

void WaterfallWidget::setFftSize(int fftSize)
{
    const int normalizedSize = std::clamp(
        fftSize, kMinimumFftSize, kMaximumFftSize);
    if (m_fft && m_fft->fftSize == normalizedSize) {
        return;
    }

    auto next = std::make_unique<FftState>(normalizedSize);
    if (!next->plan) {
        return;
    }

    m_fft = std::move(next);
    m_history.fill(ThemeManager::paletteColors().visualizationBackground);
    update();
}

int WaterfallWidget::fftSize() const noexcept
{
    return m_fft ? m_fft->fftSize : kDefaultFftSize;
}

void WaterfallWidget::pushSamples(const double* left, const double* right,
                                  std::size_t count,
                                  std::uint32_t sampleRate)
{
    if (!left || !right || count == 0 || !m_fft->plan) {
        return;
    }
    m_sampleRate = std::max<std::uint32_t>(1, sampleRate);
    const std::size_t fftSize = static_cast<std::size_t>(m_fft->fftSize);
    const int frequencyBins = m_fft->frequencyBins;
    const std::size_t available = std::min(count, fftSize);
    const std::size_t sourceStart = count - available;
    const std::size_t windowStart = fftSize - available;
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
            static_cast<int>(lowFrequency * m_fft->fftSize / m_sampleRate),
            1, frequencyBins - 1);
        const int lastBin = std::clamp(
            static_cast<int>(std::ceil(
                highFrequency * m_fft->fftSize / m_sampleRate)),
            firstBin + 1, frequencyBins);
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
    const auto& colors = ThemeManager::paletteColors();
    painter.fillRect(rect(), colors.visualizationBackground);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, false);
    painter.drawImage(rect().adjusted(1, 1, -1, -1), m_history);
    painter.setPen(QPen(colors.visualizationBorder, 1.0));
    painter.drawRect(rect().adjusted(0, 0, -1, -1));
}

} // namespace isac::ui
