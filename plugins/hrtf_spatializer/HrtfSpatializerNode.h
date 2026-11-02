#pragma once

#include <isac/plugin/IAudioNode.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <string>
#include <string_view>

#include <mysofa.h>

namespace isac::plugins {

class HrtfSpatializerNode final : public IAudioNode,
                                  public IAudioNodePreparation {
public:
    static constexpr double kMinimumAzimuthDegrees = -180.0;
    static constexpr double kMaximumAzimuthDegrees = 180.0;
    static constexpr double kMinimumElevationDegrees = -90.0;
    static constexpr double kMaximumElevationDegrees = 90.0;
    static constexpr double kMinimumSpeakerAngleDegrees = 10.0;
    static constexpr double kMaximumSpeakerAngleDegrees = 180.0;
    static constexpr double kMinimumWet = 0.0;
    static constexpr double kMaximumWet = 1.0;
    static constexpr std::uint32_t kMaximumSampleRate = 768000;
    static constexpr std::size_t kMaximumIrLength = 2048;
    static constexpr std::size_t kFilterCount = 4;
    static constexpr std::uint32_t kFilterCrossfadeFrames = 256;

    std::string name() const override { return "Stereo HRTF Spatializer"; }
    int numInputs() const override { return 2; }
    int numOutputs() const override { return 2; }

    std::string inputName(PortIndex port) const override
    {
        return port == 0 ? "Left" : "Right";
    }

    std::string outputName(PortIndex port) const override
    {
        return port == 0 ? "Left Ear" : "Right Ear";
    }

    void prepare(std::uint32_t sampleRate) override
    {
        if (sampleRate == 0 || sampleRate > kMaximumSampleRate) {
            return;
        }
        m_preparedSampleRate = sampleRate;
        refreshFilters();
    }

    bool setParameter(std::string_view name, double value) noexcept override
    {
        if (!std::isfinite(value)) {
            return false;
        }
        if (name == "azimuthDeg") {
            setAzimuthDegrees(value);
            return true;
        }
        if (name == "elevationDeg") {
            setElevationDegrees(value);
            return true;
        }
        if (name == "speakerAngleDeg") {
            setSpeakerAngleDegrees(value);
            return true;
        }
        if (name == "wet") {
            setWet(value);
            return true;
        }
        return false;
    }

    void setAzimuthDegrees(double value) noexcept
    {
        if (std::isfinite(value)) {
            m_azimuthDegrees.store(
                std::clamp(value, kMinimumAzimuthDegrees,
                           kMaximumAzimuthDegrees),
                std::memory_order_relaxed);
        }
    }

    double azimuthDegrees() const noexcept
    {
        return m_azimuthDegrees.load(std::memory_order_relaxed);
    }

    void setElevationDegrees(double value) noexcept
    {
        if (std::isfinite(value)) {
            m_elevationDegrees.store(
                std::clamp(value, kMinimumElevationDegrees,
                           kMaximumElevationDegrees),
                std::memory_order_relaxed);
        }
    }

    double elevationDegrees() const noexcept
    {
        return m_elevationDegrees.load(std::memory_order_relaxed);
    }

    void setSpeakerAngleDegrees(double value) noexcept
    {
        if (std::isfinite(value)) {
            m_speakerAngleDegrees.store(
                std::clamp(value, kMinimumSpeakerAngleDegrees,
                           kMaximumSpeakerAngleDegrees),
                std::memory_order_relaxed);
        }
    }

    double speakerAngleDegrees() const noexcept
    {
        return m_speakerAngleDegrees.load(std::memory_order_relaxed);
    }

    void setWet(double value) noexcept
    {
        if (std::isfinite(value)) {
            m_wet.store(
                std::clamp(value, kMinimumWet, kMaximumWet),
                std::memory_order_relaxed);
        }
    }

    double wet() const noexcept
    {
        return m_wet.load(std::memory_order_relaxed);
    }

    // Control-thread-only operation. The file is opened and interpolated
    // before publishing a complete filter bank to the real-time thread.
    bool setSofaPath(const std::string& path)
    {
        m_sofaPath = path;
        return refreshFilters();
    }

    const std::string& sofaPath() const noexcept { return m_sofaPath; }

    // Control-thread-only operation. Useful after position controls change.
    bool refreshFilters()
    {
        if (m_sofaPath.empty() || m_preparedSampleRate == 0) {
            m_filterReady.store(false, std::memory_order_release);
            return false;
        }

        int filterLength = 0;
        int error = MYSOFA_OK;
        MYSOFA_EASY* easy = mysofa_open(
            m_sofaPath.c_str(),
            static_cast<float>(m_preparedSampleRate),
            &filterLength,
            &error);
        if (!easy || filterLength <= 0 ||
            static_cast<std::size_t>(filterLength) > kMaximumIrLength) {
            if (easy) {
                mysofa_close(easy);
            }
            m_filterReady.store(false, std::memory_order_release);
            return false;
        }

        const int active = m_activeBank.load(std::memory_order_acquire);
        const int inactive = 1 - active;
        auto& bank = m_filterBanks[static_cast<std::size_t>(inactive)];
        for (auto& filter : bank) {
            filter.fill(0.0);
        }

        const double azimuth = azimuthDegrees();
        const double elevation = elevationDegrees();
        const double halfSpeakerAngle = speakerAngleDegrees() * 0.5;
        const std::array<double, 2> speakerAzimuths{
            azimuth - halfSpeakerAngle,
            azimuth + halfSpeakerAngle
        };
        std::array<float, kMaximumIrLength> left{};
        std::array<float, kMaximumIrLength> right{};
        int effectiveFilterLength = filterLength;
        for (std::size_t speaker = 0; speaker < speakerAzimuths.size();
             ++speaker) {
            const double azimuthRadians =
                speakerAzimuths[speaker] * (std::acos(-1.0) / 180.0);
            const double elevationRadians =
                elevation * (std::acos(-1.0) / 180.0);
            const float x = static_cast<float>(
                std::sin(azimuthRadians) * std::cos(elevationRadians));
            const float y = static_cast<float>(
                std::cos(azimuthRadians) * std::cos(elevationRadians));
            const float z = static_cast<float>(std::sin(elevationRadians));
            float delayLeft = 0.0f;
            float delayRight = 0.0f;
            mysofa_getfilter_float(
                easy, x, y, z, left.data(), right.data(),
                &delayLeft, &delayRight);
            const auto leftDelay = std::max(
                0, static_cast<int>(std::lround(
                       std::isfinite(delayLeft)
                           ? delayLeft *
                                 static_cast<float>(m_preparedSampleRate)
                           : 0.0f)));
            const auto rightDelay = std::max(
                0, static_cast<int>(std::lround(
                       std::isfinite(delayRight)
                           ? delayRight *
                                 static_cast<float>(m_preparedSampleRate)
                           : 0.0f)));
            effectiveFilterLength = std::min(
                static_cast<int>(kMaximumIrLength),
                std::max(effectiveFilterLength,
                         std::max(filterLength + leftDelay,
                                  filterLength + rightDelay)));
            for (int tap = 0; tap < filterLength; ++tap) {
                const auto leftIndex = static_cast<std::size_t>(tap) +
                                       static_cast<std::size_t>(leftDelay);
                const auto rightIndex = static_cast<std::size_t>(tap) +
                                        static_cast<std::size_t>(rightDelay);
                if (leftIndex < kMaximumIrLength) {
                    bank[speaker * 2][leftIndex] =
                        std::isfinite(left[static_cast<std::size_t>(tap)])
                        ? static_cast<double>(
                              left[static_cast<std::size_t>(tap)])
                        : 0.0;
                }
                if (rightIndex < kMaximumIrLength) {
                    bank[speaker * 2 + 1][rightIndex] =
                        std::isfinite(right[static_cast<std::size_t>(tap)])
                        ? static_cast<double>(
                              right[static_cast<std::size_t>(tap)])
                        : 0.0;
                }
            }
        }
        mysofa_close(easy);

        m_filterLength.store(effectiveFilterLength,
                             std::memory_order_relaxed);
        m_activeBank.store(inactive, std::memory_order_release);
        m_filterReady.store(true, std::memory_order_release);
        return true;
    }

    void process(const AudioBuffer& inBus, AudioBuffer& outBus,
                 int frames) override
    {
        const int count = std::clamp(
            frames, 0, std::min(inBus.numFrames(), outBus.numFrames()));
        if (count <= 0 || inBus.numChannels() < 2 ||
            outBus.numChannels() < 2) {
            return;
        }

        const AudioSample* inputLeft = inBus.channel(0);
        const AudioSample* inputRight = inBus.channel(1);
        AudioSample* outputLeft = outBus.channel(0);
        AudioSample* outputRight = outBus.channel(1);
        const double wetValue = m_wet.load(std::memory_order_relaxed);
        const bool ready = m_filterReady.load(std::memory_order_acquire);
        const int publishedBank =
            m_activeBank.load(std::memory_order_acquire);
        const int filterLength = std::clamp(
            m_filterLength.load(std::memory_order_relaxed), 1,
            static_cast<int>(kMaximumIrLength));

        if (!ready) {
            for (int frame = 0; frame < count; ++frame) {
                outputLeft[frame] = finiteOrZero(inputLeft[frame]);
                outputRight[frame] = finiteOrZero(inputRight[frame]);
            }
            return;
        }

        if (publishedBank != m_rtBank) {
            m_transitionFrom = m_rtFilters;
            m_rtFilters = m_filterBanks[static_cast<std::size_t>(publishedBank)];
            m_rtBank = publishedBank;
            m_transitionRemaining = kFilterCrossfadeFrames;
        }

        for (int frame = 0; frame < count; ++frame) {
            m_history[0][m_historyIndex] = finiteOrZero(inputLeft[frame]);
            m_history[1][m_historyIndex] = finiteOrZero(inputRight[frame]);

            const AudioSample newLeft = convolve(0, 0, filterLength) +
                                         convolve(2, 1, filterLength);
            const AudioSample newRight = convolve(1, 0, filterLength) +
                                          convolve(3, 1, filterLength);
            AudioSample spatialLeft = newLeft;
            AudioSample spatialRight = newRight;
            if (m_transitionRemaining > 0) {
                const AudioSample oldLeft =
                    convolveFrom(0, 0, filterLength) +
                    convolveFrom(2, 1, filterLength);
                const AudioSample oldRight =
                    convolveFrom(1, 0, filterLength) +
                    convolveFrom(3, 1, filterLength);
                const double newMix =
                    1.0 - static_cast<double>(m_transitionRemaining) /
                        static_cast<double>(kFilterCrossfadeFrames);
                spatialLeft = oldLeft * (1.0 - newMix) + newLeft * newMix;
                spatialRight = oldRight * (1.0 - newMix) + newRight * newMix;
                --m_transitionRemaining;
            }

            const AudioSample dryLeft = m_history[0][m_historyIndex];
            const AudioSample dryRight = m_history[1][m_historyIndex];
            outputLeft[frame] = finiteOrZero(
                dryLeft * (1.0 - wetValue) + spatialLeft * wetValue);
            outputRight[frame] = finiteOrZero(
                dryRight * (1.0 - wetValue) + spatialRight * wetValue);
            m_historyIndex = (m_historyIndex + 1) % kMaximumIrLength;
        }
    }

private:
    using Filter = std::array<AudioSample, kMaximumIrLength>;
    using FilterBank = std::array<Filter, kFilterCount>;

    static AudioSample finiteOrZero(AudioSample value) noexcept
    {
        return std::isfinite(value) ? value : 0.0;
    }

    AudioSample convolve(std::size_t filterIndex, std::size_t inputIndex,
                         int filterLength) const noexcept
    {
        return convolveWith(
            m_rtFilters[filterIndex], inputIndex, filterLength);
    }

    AudioSample convolveFrom(std::size_t filterIndex,
                             std::size_t inputIndex,
                             int filterLength) const noexcept
    {
        return convolveWith(
            m_transitionFrom[filterIndex], inputIndex, filterLength);
    }

    AudioSample convolveWith(const Filter& filter, std::size_t inputIndex,
                             int filterLength) const noexcept
    {
        AudioSample sum = 0.0;
        std::size_t historyIndex = m_historyIndex;
        for (int tap = 0; tap < filterLength; ++tap) {
            sum += filter[static_cast<std::size_t>(tap)] *
                   m_history[inputIndex][historyIndex];
            if (historyIndex == 0) {
                historyIndex = kMaximumIrLength - 1;
            } else {
                --historyIndex;
            }
        }
        return finiteOrZero(sum);
    }

    std::atomic<double> m_azimuthDegrees{0.0};
    std::atomic<double> m_elevationDegrees{0.0};
    std::atomic<double> m_speakerAngleDegrees{60.0};
    std::atomic<double> m_wet{1.0};
    std::atomic<int> m_activeBank{0};
    std::atomic<int> m_filterLength{1};
    std::atomic<bool> m_filterReady{false};
    std::uint32_t m_preparedSampleRate = 0;
    std::string m_sofaPath;

    std::array<FilterBank, 2> m_filterBanks{};
    FilterBank m_rtFilters{};
    FilterBank m_transitionFrom{};
    std::array<std::array<AudioSample, kMaximumIrLength>, 2> m_history{};
    std::size_t m_historyIndex = 0;
    int m_rtBank = -1;
    std::uint32_t m_transitionRemaining = 0;
};

static_assert(std::atomic<double>::is_always_lock_free,
              "HRTF parameters must use lock-free atomics");

} // namespace isac::plugins
