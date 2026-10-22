#pragma once

#include <QImage>
#include <QWidget>
#include <memory>
#include <vector>

namespace isac::ui {

class WaveformProgressWidget final : public QWidget {
    Q_OBJECT
public:
    explicit WaveformProgressWidget(QWidget* parent = nullptr);

    void setSamples(const double* samples, std::size_t count);
    void setProgress(double progress);

signals:
    void seekRequested(double progress);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;

private:
    void seekAt(qreal x);
    std::vector<double> m_samples;
    double m_progress = 0.0;
    bool m_dragging = false;
};

class WaterfallWidget final : public QWidget {
    Q_OBJECT
public:
    explicit WaterfallWidget(QWidget* parent = nullptr);
    ~WaterfallWidget() override;

    void pushSamples(const double* left, const double* right,
                     std::size_t count, std::uint32_t sampleRate);

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    struct FftState;
    std::unique_ptr<FftState> m_fft;
    QImage m_history;
    std::uint32_t m_sampleRate = 48000;
};

} // namespace isac::ui
