#pragma once

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace isac {

class AudioTapBuffer {
public:
    AudioTapBuffer(std::size_t capacityFrames, std::size_t channels)
        : m_capacity(std::max<std::size_t>(1, capacityFrames))
        , m_channels(std::max<std::size_t>(1, channels))
        , m_samples(m_capacity * m_channels, 0.0)
    {
    }

    void write(const double* const* channels,
               std::size_t frames) noexcept
    {
        const std::uint64_t write =
            m_writeCursor.load(std::memory_order_relaxed);
        const std::uint64_t read =
            m_readCursor.load(std::memory_order_acquire);
        const std::size_t occupied =
            static_cast<std::size_t>(write - read);
        const std::size_t count =
            std::min(frames, m_capacity - std::min(occupied, m_capacity));
        const std::size_t offset =
            static_cast<std::size_t>(write % m_capacity);
        const std::size_t first =
            std::min(count, m_capacity - offset);

        copyInto(channels, offset, 0, first);
        copyInto(channels, 0, first, count - first);
        m_writeCursor.store(write + count, std::memory_order_release);
    }

    std::size_t readLatest(double* const* channels,
                           std::size_t maxFrames) noexcept
    {
        const std::uint64_t read =
            m_readCursor.load(std::memory_order_relaxed);
        const std::uint64_t write =
            m_writeCursor.load(std::memory_order_acquire);
        const std::size_t available =
            static_cast<std::size_t>(write - read);
        const std::size_t count = std::min(maxFrames, available);
        const std::uint64_t start = write - count;
        const std::size_t offset =
            static_cast<std::size_t>(start % m_capacity);
        const std::size_t first =
            std::min(count, m_capacity - offset);

        copyOut(channels, offset, 0, first);
        copyOut(channels, 0, first, count - first);
        m_readCursor.store(write, std::memory_order_release);
        return count;
    }

    void reset() noexcept
    {
        const auto write = m_writeCursor.load(std::memory_order_acquire);
        m_readCursor.store(write, std::memory_order_release);
    }

private:
    void copyInto(const double* const* source, std::size_t sourceOffset,
                  std::size_t destinationOffset,
                  std::size_t frames) noexcept
    {
        for (std::size_t frame = 0; frame < frames; ++frame) {
            for (std::size_t channel = 0; channel < m_channels; ++channel) {
                m_samples[((sourceOffset + frame) % m_capacity) * m_channels
                          + channel] = source[channel][destinationOffset + frame];
            }
        }
    }

    void copyOut(double* const* destination, std::size_t sourceOffset,
                 std::size_t destinationOffset,
                 std::size_t frames) const noexcept
    {
        for (std::size_t frame = 0; frame < frames; ++frame) {
            for (std::size_t channel = 0; channel < m_channels; ++channel) {
                destination[channel][destinationOffset + frame] =
                    m_samples[((sourceOffset + frame) % m_capacity) *
                                  m_channels +
                              channel];
            }
        }
    }

    const std::size_t m_capacity;
    const std::size_t m_channels;
    std::vector<double> m_samples;
    alignas(64) std::atomic<std::uint64_t> m_writeCursor{0};
    alignas(64) std::atomic<std::uint64_t> m_readCursor{0};
};

} // namespace isac
