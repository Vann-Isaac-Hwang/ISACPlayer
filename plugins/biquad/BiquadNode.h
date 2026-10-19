#pragma once

#include <isac/plugin/IAudioNode.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <limits>
#include <string_view>

namespace isac::plugins {

enum class BiquadType : uint8_t {
    Bypass = 0,
    LowShelf,
    HighShelf,
    Peak,
    Notch
};

class BiquadNode final : public IAudioNode {
public:
    static constexpr int kBandCount = 10;

    BiquadNode()
    {
        constexpr std::array<double, kBandCount> frequencies{
            31.0, 62.0, 125.0, 250.0, 500.0,
            1000.0, 2000.0, 4000.0, 8000.0, 16000.0
        };
        for (int band = 0; band < kBandCount; ++band) {
            m_bands[band].frequency.store(frequencies[band], std::memory_order_relaxed);
            m_bands[band].type.store(BiquadType::Peak, std::memory_order_relaxed);
        }
    }

    std::string name() const override { return "Biquad Equalizer (10 Band)"; }
    int numInputs() const override { return 2; }
    int numOutputs() const override { return 2; }

    std::string inputName(PortIndex port) const override {
        return port == 0 ? "In L" : "In R";
    }

    std::string outputName(PortIndex port) const override {
        return port == 0 ? "Out L" : "Out R";
    }

    bool setParameter(std::string_view name, double value) noexcept override
    {
        if (name == "bypass") {
            m_bypassed.store(value >= 0.5, std::memory_order_relaxed);
            return true;
        }
        return false;
    }

    void setBypassed(bool bypassed) noexcept
    {
        m_bypassed.store(bypassed, std::memory_order_relaxed);
    }

    bool bypassed() const noexcept
    {
        return m_bypassed.load(std::memory_order_relaxed);
    }

    void setBandType(int band, BiquadType type) noexcept
    {
        if (validBand(band)) {
            m_bands[band].type.store(type, std::memory_order_relaxed);
        }
    }

    void setBandFrequency(int band, double frequency) noexcept
    {
        if (validBand(band)) {
            m_bands[band].frequency.store(sanitizeFrequency(frequency),
                                          std::memory_order_relaxed);
        }
    }

    void setBandGainDb(int band, double gainDb) noexcept
    {
        if (validBand(band)) {
            m_bands[band].gain.store(sanitizeGain(gainDb),
                                     std::memory_order_relaxed);
        }
    }

    void setBandQ(int band, double q) noexcept
    {
        if (validBand(band)) {
            m_bands[band].q.store(sanitizeQ(q), std::memory_order_relaxed);
        }
    }

    BiquadType bandType(int band) const noexcept
    {
        return validBand(band)
            ? m_bands[band].type.load(std::memory_order_relaxed)
            : BiquadType::Bypass;
    }

    double bandFrequency(int band) const noexcept
    {
        return validBand(band)
            ? m_bands[band].frequency.load(std::memory_order_relaxed)
            : 1000.0;
    }

    double bandGainDb(int band) const noexcept
    {
        return validBand(band)
            ? m_bands[band].gain.load(std::memory_order_relaxed)
            : 0.0;
    }

    double bandQ(int band) const noexcept
    {
        return validBand(band)
            ? m_bands[band].q.load(std::memory_order_relaxed)
            : 1.0;
    }

    void process(const AudioBuffer& inBus, AudioBuffer& outBus, int frames) override
    {
        const int count = std::clamp(
            frames, 0, std::min(inBus.numFrames(), outBus.numFrames()));
        if (count <= 0) {
            return;
        }

        if (m_bypassed.load(std::memory_order_relaxed)) {
            copyStereo(inBus, outBus, count);
            return;
        }

        const double sampleRate = std::max(
            1.0, static_cast<double>(inBus.sampleRate()));
        std::array<Coefficients, kBandCount> coefficients{};
        std::array<bool, kBandCount> active{};

        for (int band = 0; band < kBandCount; ++band) {
            const auto type = m_bands[band].type.load(std::memory_order_relaxed);
            active[band] = type != BiquadType::Bypass;
            coefficients[band] = makeCoefficients(
                type,
                m_bands[band].frequency.load(std::memory_order_relaxed),
                m_bands[band].gain.load(std::memory_order_relaxed),
                m_bands[band].q.load(std::memory_order_relaxed),
                sampleRate);
        }

        for (int channel = 0; channel < 2; ++channel) {
            const AudioSample* input = inBus.channel(channel);
            AudioSample* output = outBus.channel(channel);
            for (int frame = 0; frame < count; ++frame) {
                AudioSample sample = input[frame];
                for (int band = 0; band < kBandCount; ++band) {
                    if (active[band]) {
                        sample = processSample(
                            sample, coefficients[band], m_states[band][channel]);
                    }
                }
                output[frame] = sample;
            }
        }
    }

private:
    struct Parameters {
        std::atomic<double> frequency{1000.0};
        std::atomic<double> gain{0.0};
        std::atomic<double> q{1.0};
        std::atomic<BiquadType> type{BiquadType::Bypass};
    };

    struct Coefficients {
        double b0 = 1.0;
        double b1 = 0.0;
        double b2 = 0.0;
        double a1 = 0.0;
        double a2 = 0.0;
    };

    struct State {
        double x1 = 0.0;
        double x2 = 0.0;
        double y1 = 0.0;
        double y2 = 0.0;
    };

    static bool validBand(int band) noexcept
    {
        return band >= 0 && band < kBandCount;
    }

    static double sanitizeFrequency(double value) noexcept
    {
        return std::isfinite(value) ? std::clamp(value, 1.0, 24000.0) : 1000.0;
    }

    static double sanitizeGain(double value) noexcept
    {
        return std::isfinite(value) ? std::clamp(value, -60.0, 60.0) : 0.0;
    }

    static double sanitizeQ(double value) noexcept
    {
        return std::isfinite(value) ? std::clamp(value, 0.001, 100.0) : 1.0;
    }

    static Coefficients makeCoefficients(
        BiquadType type, double frequency, double gainDb, double q,
        double sampleRate) noexcept
    {
        if (type == BiquadType::Bypass) {
            return {};
        }

        const double nyquist = sampleRate * 0.5;
        frequency = std::clamp(frequency, 1.0, std::max(1.0, nyquist * 0.999));
        const double omega = 2.0 * std::acos(-1.0) * frequency / sampleRate;
        const double sine = std::sin(omega);
        const double cosine = std::cos(omega);
        const double amplitude = std::pow(10.0, gainDb / 40.0);
        const double alpha = sine / (2.0 * std::max(q, 0.001));
        const double beta = 2.0 * std::sqrt(amplitude) * alpha;

        double b0 = 1.0;
        double b1 = 0.0;
        double b2 = 0.0;
        double a0 = 1.0;
        double a1 = 0.0;
        double a2 = 0.0;

        switch (type) {
        case BiquadType::LowShelf:
            b0 = amplitude * ((amplitude + 1.0) -
                              (amplitude - 1.0) * cosine + beta);
            b1 = 2.0 * amplitude * ((amplitude - 1.0) -
                                     (amplitude + 1.0) * cosine);
            b2 = amplitude * ((amplitude + 1.0) -
                              (amplitude - 1.0) * cosine - beta);
            a0 = (amplitude + 1.0) +
                 (amplitude - 1.0) * cosine + beta;
            a1 = -2.0 * ((amplitude - 1.0) +
                         (amplitude + 1.0) * cosine);
            a2 = (amplitude + 1.0) +
                 (amplitude - 1.0) * cosine - beta;
            break;
        case BiquadType::HighShelf:
            b0 = amplitude * ((amplitude + 1.0) +
                              (amplitude - 1.0) * cosine + beta);
            b1 = -2.0 * amplitude * ((amplitude - 1.0) +
                                     (amplitude + 1.0) * cosine);
            b2 = amplitude * ((amplitude + 1.0) +
                              (amplitude - 1.0) * cosine - beta);
            a0 = (amplitude + 1.0) -
                 (amplitude - 1.0) * cosine + beta;
            a1 = 2.0 * ((amplitude - 1.0) -
                        (amplitude + 1.0) * cosine);
            a2 = (amplitude + 1.0) -
                 (amplitude - 1.0) * cosine - beta;
            break;
        case BiquadType::Notch:
            b0 = 1.0;
            b1 = -2.0 * cosine;
            b2 = 1.0;
            a0 = 1.0 + alpha;
            a1 = -2.0 * cosine;
            a2 = 1.0 - alpha;
            break;
        case BiquadType::Peak:
            b0 = 1.0 + alpha * amplitude;
            b1 = -2.0 * cosine;
            b2 = 1.0 - alpha * amplitude;
            a0 = 1.0 + alpha / amplitude;
            a1 = -2.0 * cosine;
            a2 = 1.0 - alpha / amplitude;
            break;
        case BiquadType::Bypass:
            break;
        }

        if (!std::isfinite(a0) ||
            std::abs(a0) < std::numeric_limits<double>::epsilon()) {
            return {};
        }

        const double inverseA0 = 1.0 / a0;
        return {b0 * inverseA0, b1 * inverseA0, b2 * inverseA0,
                a1 * inverseA0, a2 * inverseA0};
    }

    static AudioSample processSample(
        AudioSample input, const Coefficients& coefficients, State& state) noexcept
    {
        const AudioSample output =
            coefficients.b0 * input +
            coefficients.b1 * state.x1 +
            coefficients.b2 * state.x2 -
            coefficients.a1 * state.y1 -
            coefficients.a2 * state.y2;
        state.x2 = state.x1;
        state.x1 = input;
        state.y2 = state.y1;
        state.y1 = output;
        return output;
    }

    static void copyStereo(
        const AudioBuffer& input, AudioBuffer& output, int frames) noexcept
    {
        for (int channel = 0; channel < 2; ++channel) {
            std::copy_n(input.channel(channel), frames, output.channel(channel));
        }
    }

    std::array<Parameters, kBandCount> m_bands{};
    std::array<std::array<State, 2>, kBandCount> m_states{};
    std::atomic<bool> m_bypassed{false};
};

static_assert(std::atomic<double>::is_always_lock_free,
              "Biquad parameters must use lock-free atomics");

} // namespace isac::plugins
