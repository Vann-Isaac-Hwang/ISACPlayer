#pragma once

#include <isac/plugin/IAudioNode.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

namespace isac::plugins {

class MonoModulatedDelayNode final : public IAudioNode {
public:
    static constexpr double kMinimumBaseDelayMilliseconds = 10.0;
    static constexpr double kMaximumBaseDelayMilliseconds = 50.0;
    static constexpr double kMaximumDepthMilliseconds = 9.0;
    static constexpr double kMinimumRateHz = 0.05;
    static constexpr double kMaximumRateHz = 10.0;
    static constexpr std::uint32_t kMaximumSampleRate = 768000;
    static constexpr std::size_t kSineTableSize = 2048;

    MonoModulatedDelayNode()
        : m_delayLine(static_cast<std::size_t>(
              kMaximumDelayFrames + 2), AudioSample{0.0})
    {
        const double radiansPerSample =
            2.0 * std::acos(-1.0) /
            static_cast<double>(kSineTableSize);
        for (std::size_t index = 0; index < m_sineTable.size(); ++index) {
            m_sineTable[index] =
                std::sin(radiansPerSample * static_cast<double>(index));
        }
    }

    std::string name() const override { return "Mono Modulated Delay"; }
    int numInputs() const override { return 1; }
    int numOutputs() const override { return 1; }

    std::string inputName(PortIndex) const override { return "In"; }
    std::string outputName(PortIndex) const override { return "Modulated Out"; }

    bool setParameter(std::string_view name, double value) noexcept override
    {
        if (name == "baseDelayMs") {
            setBaseDelayMilliseconds(value);
            return true;
        }
        if (name == "depthMs") {
            setDepthMilliseconds(value);
            return true;
        }
        if (name == "rateHz") {
            setRateHz(value);
            return true;
        }
        if (name == "phaseDeg") {
            setPhaseDegrees(value);
            return true;
        }
        return false;
    }

    void setBaseDelayMilliseconds(double milliseconds) noexcept
    {
        if (!std::isfinite(milliseconds)) {
            return;
        }
        m_baseDelayMilliseconds.store(
            std::clamp(milliseconds, kMinimumBaseDelayMilliseconds,
                       kMaximumBaseDelayMilliseconds),
            std::memory_order_relaxed);
    }

    double baseDelayMilliseconds() const noexcept
    {
        return m_baseDelayMilliseconds.load(std::memory_order_relaxed);
    }

    void setDepthMilliseconds(double milliseconds) noexcept
    {
        if (!std::isfinite(milliseconds)) {
            return;
        }
        m_depthMilliseconds.store(
            std::clamp(milliseconds, 0.0, kMaximumDepthMilliseconds),
            std::memory_order_relaxed);
    }

    double depthMilliseconds() const noexcept
    {
        return m_depthMilliseconds.load(std::memory_order_relaxed);
    }

    void setRateHz(double rate) noexcept
    {
        if (!std::isfinite(rate)) {
            return;
        }
        m_rateHz.store(
            std::clamp(rate, kMinimumRateHz, kMaximumRateHz),
            std::memory_order_relaxed);
    }

    double rateHz() const noexcept
    {
        return m_rateHz.load(std::memory_order_relaxed);
    }

    void setPhaseDegrees(double degrees) noexcept
    {
        if (!std::isfinite(degrees)) {
            return;
        }
        m_phaseDegrees.store(
            std::clamp(degrees, 0.0, 360.0),
            std::memory_order_relaxed);
    }

    double phaseDegrees() const noexcept
    {
        return m_phaseDegrees.load(std::memory_order_relaxed);
    }

    void process(const AudioBuffer& inBus, AudioBuffer& outBus,
                 int frames) override
    {
        const int count = std::clamp(
            frames, 0, std::min(inBus.numFrames(), outBus.numFrames()));
        if (count <= 0 || inBus.numChannels() < 1 ||
            outBus.numChannels() < 1) {
            return;
        }

        const std::uint32_t sampleRate =
            std::max<std::uint32_t>(1, inBus.sampleRate());
        const double rate = m_rateHz.load(std::memory_order_relaxed);
        const double targetBaseDelayFrames =
            m_baseDelayMilliseconds.load(std::memory_order_relaxed) *
            static_cast<double>(sampleRate) / 1000.0;
        const double targetDepthFrames =
            m_depthMilliseconds.load(std::memory_order_relaxed) *
            static_cast<double>(sampleRate) / 1000.0;

        if (m_lastSampleRate != sampleRate) {
            m_currentBaseDelayFrames = targetBaseDelayFrames;
            m_currentDepthFrames = targetDepthFrames;
            m_lastSampleRate = sampleRate;
        }

        const double baseDelayStep =
            (targetBaseDelayFrames - m_currentBaseDelayFrames) /
            static_cast<double>(count);
        const double depthStep =
            (targetDepthFrames - m_currentDepthFrames) /
            static_cast<double>(count);
        const double phaseStep = rate / static_cast<double>(sampleRate);
        const double targetPhaseOffset =
            m_phaseDegrees.load(std::memory_order_relaxed) / 360.0;
        double phaseOffsetDifference =
            targetPhaseOffset - m_currentPhaseOffset;
        if (phaseOffsetDifference > 0.5) {
            phaseOffsetDifference -= 1.0;
        } else if (phaseOffsetDifference < -0.5) {
            phaseOffsetDifference += 1.0;
        }
        const double phaseOffsetStep =
            phaseOffsetDifference / static_cast<double>(count);
        const AudioSample* input = inBus.channel(0);
        AudioSample* output = outBus.channel(0);

        for (int frame = 0; frame < count; ++frame) {
            m_currentBaseDelayFrames += baseDelayStep;
            m_currentDepthFrames += depthStep;
            m_currentPhaseOffset += phaseOffsetStep;
            if (m_currentPhaseOffset < 0.0) {
                m_currentPhaseOffset += 1.0;
            } else if (m_currentPhaseOffset >= 1.0) {
                m_currentPhaseOffset -= 1.0;
            }

            double lookupPhase = m_phase + m_currentPhaseOffset;
            if (lookupPhase >= 1.0) {
                lookupPhase -= 1.0;
            }
            const double lfo = readSine(lookupPhase);
            const double delayFrames = std::clamp(
                m_currentBaseDelayFrames + m_currentDepthFrames * lfo,
                1.0, static_cast<double>(kMaximumDelayFrames));
            const AudioSample delayed = readDelay(delayFrames);
            const AudioSample source =
                std::isfinite(input[frame]) ? input[frame] : 0.0;

            output[frame] = std::isfinite(delayed) ? delayed : 0.0;
            m_delayLine[m_writeIndex] = source;
            m_writeIndex = (m_writeIndex + 1) % m_delayLine.size();

            m_phase += phaseStep;
            if (m_phase >= 1.0) {
                m_phase -=
                    static_cast<double>(static_cast<std::uint64_t>(m_phase));
            }
        }

        m_currentBaseDelayFrames = targetBaseDelayFrames;
        m_currentDepthFrames = targetDepthFrames;
    }

private:
    static constexpr std::size_t kMaximumDelayFrames =
        static_cast<std::size_t>(
            kMaximumSampleRate *
            (kMaximumBaseDelayMilliseconds + kMaximumDepthMilliseconds) /
            1000.0);

    double readSine(double phase) const noexcept
    {
        const double tablePosition =
            phase * static_cast<double>(kSineTableSize);
        const auto index = static_cast<std::size_t>(tablePosition);
        const auto nextIndex = (index + 1) % kSineTableSize;
        const double fraction =
            tablePosition - static_cast<double>(index);
        return m_sineTable[index] * (1.0 - fraction) +
               m_sineTable[nextIndex] * fraction;
    }

    AudioSample historySample(std::size_t framesBehind) const noexcept
    {
        const std::size_t size = m_delayLine.size();
        if (framesBehind >= size) {
            return 0.0;
        }
        const std::size_t offset = framesBehind % size;
        const std::size_t index =
            (m_writeIndex + size - offset) % size;
        return m_delayLine[index];
    }

    AudioSample readDelay(double delayFrames) const noexcept
    {
        const auto wholeDelay = static_cast<std::size_t>(delayFrames);
        const double fraction =
            delayFrames - static_cast<double>(wholeDelay);
        const AudioSample earlier = historySample(wholeDelay);
        const AudioSample later = historySample(wholeDelay + 1);
        return earlier * (1.0 - fraction) + later * fraction;
    }

    std::atomic<double> m_baseDelayMilliseconds{20.0};
    std::atomic<double> m_depthMilliseconds{3.0};
    std::atomic<double> m_rateHz{0.25};
    std::atomic<double> m_phaseDegrees{0.0};
    std::array<double, kSineTableSize> m_sineTable{};
    std::vector<AudioSample> m_delayLine;
    std::size_t m_writeIndex = 0;
    double m_phase = 0.0;
    double m_currentPhaseOffset = 0.0;
    double m_currentBaseDelayFrames = 0.0;
    double m_currentDepthFrames = 0.0;
    std::uint32_t m_lastSampleRate = 0;
};

static_assert(std::atomic<double>::is_always_lock_free,
              "Mono Modulated Delay parameters must use lock-free atomics");

} // namespace isac::plugins
