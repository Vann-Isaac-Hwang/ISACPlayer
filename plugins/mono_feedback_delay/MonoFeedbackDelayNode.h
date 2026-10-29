#pragma once

#include <isac/plugin/IAudioNode.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

namespace isac::plugins {

class MonoFeedbackDelayNode final : public IAudioNode {
public:
    static constexpr double kMinimumDelayMilliseconds = 0.1;
    static constexpr double kMaximumDelayMilliseconds = 100.0;
    static constexpr double kMaximumFeedback = 0.95;
    static constexpr std::uint32_t kMaximumSampleRate = 768000;

    MonoFeedbackDelayNode()
        : m_delayLine(static_cast<std::size_t>(
              kMaximumDelayFrames + 2), AudioSample{0.0})
    {
    }

    std::string name() const override { return "Mono Feedback Delay"; }
    int numInputs() const override { return 1; }
    int numOutputs() const override { return 1; }

    std::string inputName(PortIndex) const override { return "In"; }
    std::string outputName(PortIndex) const override { return "Wet Out"; }

    bool setParameter(std::string_view name, double value) noexcept override
    {
        if (name == "delayMs") {
            setDelayMilliseconds(value);
            return true;
        }
        if (name == "feedback") {
            setFeedback(value);
            return true;
        }
        return false;
    }

    void setDelayMilliseconds(double milliseconds) noexcept
    {
        if (!std::isfinite(milliseconds)) {
            return;
        }
        m_delayMilliseconds.store(
            std::clamp(milliseconds, kMinimumDelayMilliseconds,
                       kMaximumDelayMilliseconds),
            std::memory_order_relaxed);
    }

    double delayMilliseconds() const noexcept
    {
        return m_delayMilliseconds.load(std::memory_order_relaxed);
    }

    void setFeedback(double feedback) noexcept
    {
        if (!std::isfinite(feedback)) {
            return;
        }
        m_feedback.store(
            std::clamp(feedback, -kMaximumFeedback, kMaximumFeedback),
            std::memory_order_relaxed);
    }

    double feedback() const noexcept
    {
        return m_feedback.load(std::memory_order_relaxed);
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
        const double targetDelayFrames = std::clamp(
            delayMilliseconds() * static_cast<double>(sampleRate) / 1000.0,
            1.0, static_cast<double>(kMaximumDelayFrames));
        if (m_lastSampleRate != sampleRate) {
            m_currentDelayFrames = targetDelayFrames;
            m_lastSampleRate = sampleRate;
        }

        const double delayStep =
            (targetDelayFrames - m_currentDelayFrames) /
            static_cast<double>(count);
        const double feedbackValue = feedback();
        const AudioSample* input = inBus.channel(0);
        AudioSample* output = outBus.channel(0);

        for (int frame = 0; frame < count; ++frame) {
            m_currentDelayFrames += delayStep;
            const AudioSample delayed =
                readDelay(m_currentDelayFrames);
            const AudioSample source =
                std::isfinite(input[frame]) ? input[frame] : 0.0;
            const AudioSample feedbackInput =
                source + feedbackValue * delayed;

            output[frame] = std::isfinite(delayed) ? delayed : 0.0;
            m_delayLine[m_writeIndex] =
                std::isfinite(feedbackInput) ? feedbackInput : 0.0;
            m_writeIndex = (m_writeIndex + 1) % m_delayLine.size();
        }

        m_currentDelayFrames = targetDelayFrames;
    }

private:
    static constexpr std::size_t kMaximumDelayFrames =
        static_cast<std::size_t>(
            kMaximumSampleRate * kMaximumDelayMilliseconds / 1000.0);

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

    std::atomic<double> m_delayMilliseconds{20.0};
    std::atomic<double> m_feedback{0.35};
    std::vector<AudioSample> m_delayLine;
    std::size_t m_writeIndex = 0;
    double m_currentDelayFrames = 0.0;
    std::uint32_t m_lastSampleRate = 0;
};

static_assert(std::atomic<double>::is_always_lock_free,
              "Mono Feedback Delay parameters must use lock-free atomics");

} // namespace isac::plugins
