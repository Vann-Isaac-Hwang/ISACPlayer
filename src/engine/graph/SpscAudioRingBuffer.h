#pragma once

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace isac {

class SpscAudioRingBuffer {
public:
    SpscAudioRingBuffer(std::size_t capacityFrames, std::size_t channels)
        : m_capacity(std::max<std::size_t>(1, capacityFrames))
        , m_channels(std::max<std::size_t>(1, channels))
        , m_storage(m_capacity * m_channels, 0.0)
    {
    }

    std::size_t capacityFrames() const noexcept { return m_capacity; }

    std::size_t readableFrames() const noexcept
    {
        const auto write = m_writeCursor.load(std::memory_order_acquire);
        const auto read = m_readCursor.load(std::memory_order_acquire);
        return static_cast<std::size_t>(write - read);
    }

    std::size_t writableFrames() const noexcept
    {
        return m_capacity - readableFrames();
    }

    std::size_t write(const float* interleaved, std::size_t frames) noexcept
    {
        const auto write = m_writeCursor.load(std::memory_order_relaxed);
        const auto read = m_readCursor.load(std::memory_order_acquire);
        const auto count = std::min(frames, m_capacity - static_cast<std::size_t>(write - read));
        copyIntoRing(interleaved, write, count);
        m_writeCursor.store(write + count, std::memory_order_release);
        return count;
    }

    std::size_t readPlanar(double* const* channels, std::size_t frames) noexcept
    {
        const auto read = m_readCursor.load(std::memory_order_relaxed);
        const auto write = m_writeCursor.load(std::memory_order_acquire);
        const auto count = std::min(frames, static_cast<std::size_t>(write - read));
        copyFromRing(channels, read, count);
        m_readCursor.store(read + count, std::memory_order_release);
        return count;
    }

    void discardReadableFrames() noexcept
    {
        m_readCursor.store(m_writeCursor.load(std::memory_order_acquire),
                           std::memory_order_release);
    }

    void reset() noexcept
    {
        m_readCursor.store(0, std::memory_order_relaxed);
        m_writeCursor.store(0, std::memory_order_relaxed);
    }

private:
    void copyIntoRing(const float* source, std::uint64_t cursor,
                      std::size_t frames) noexcept
    {
        const auto offset = static_cast<std::size_t>(cursor % m_capacity);
        const auto firstFrames = std::min(frames, m_capacity - offset);
        writeInterleaved(source, offset, firstFrames);
        writeInterleaved(source + firstFrames * m_channels, 0,
                         frames - firstFrames);
    }

    void copyFromRing(double* const* channels, std::uint64_t cursor,
                      std::size_t frames) const noexcept
    {
        const auto offset = static_cast<std::size_t>(cursor % m_capacity);
        const auto firstFrames = std::min(frames, m_capacity - offset);
        readPlanarSegment(channels, offset, 0, firstFrames);
        readPlanarSegment(channels, 0, firstFrames, frames - firstFrames);
    }

    void writeInterleaved(const float* source, std::size_t offset,
                          std::size_t frames) noexcept
    {
        for (std::size_t frame = 0; frame < frames; ++frame) {
            for (std::size_t channel = 0; channel < m_channels; ++channel) {
                m_storage[(offset + frame) * m_channels + channel] =
                    static_cast<double>(source[frame * m_channels + channel]);
            }
        }
    }

    void readPlanarSegment(double* const* channels, std::size_t offset,
                           std::size_t destinationOffset,
                           std::size_t frames) const noexcept
    {
        for (std::size_t frame = 0; frame < frames; ++frame) {
            for (std::size_t channel = 0; channel < m_channels; ++channel) {
                channels[channel][destinationOffset + frame] =
                    m_storage[(offset + frame) * m_channels + channel];
            }
        }
    }

    const std::size_t m_capacity;
    const std::size_t m_channels;
    std::vector<double> m_storage;
    alignas(64) std::atomic<std::uint64_t> m_writeCursor{0};
    alignas(64) std::atomic<std::uint64_t> m_readCursor{0};
};

} // namespace isac
