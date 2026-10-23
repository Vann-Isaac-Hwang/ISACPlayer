#pragma once

#include <algorithm>
#include <optional>
#include <random>
#include <utility>
#include <vector>
#include <QtGlobal>

namespace isac::library {

enum class PlaybackMode {
    Sequential,
    Shuffle,
    RepeatOne,
    RepeatAll
};

class PlaybackQueue {
public:
    void reset(std::vector<qint64> trackIds, qint64 currentTrackId,
               PlaybackMode mode)
    {
        m_mode = mode;
        m_sourceOrder = std::move(trackIds);
        m_order = m_sourceOrder;
        if (m_mode == PlaybackMode::Shuffle) {
            shuffleFromCurrent(currentTrackId);
        } else {
            setPositionForTrack(currentTrackId);
        }
    }

    void restore(std::vector<qint64> trackIds, std::size_t position,
                 PlaybackMode mode)
    {
        auto sourceTrackIds = trackIds;
        restore(std::move(trackIds), std::move(sourceTrackIds), position,
                mode);
    }

    void restore(std::vector<qint64> orderedTrackIds,
                 std::vector<qint64> sourceTrackIds, std::size_t position,
                 PlaybackMode mode)
    {
        m_mode = mode;
        m_order = std::move(orderedTrackIds);
        m_sourceOrder = std::move(sourceTrackIds);
        if (m_sourceOrder.empty() && !m_order.empty()) {
            m_sourceOrder = m_order;
        }
        m_position = m_order.empty()
            ? 0 : std::min(position, m_order.size() - 1);
    }

    void setMode(PlaybackMode mode)
    {
        if (m_mode == mode) {
            return;
        }
        const qint64 current = currentTrackId().value_or(0);
        m_mode = mode;
        if (m_mode == PlaybackMode::Shuffle) {
            shuffleFromCurrent(current);
        } else if (m_mode == PlaybackMode::Sequential) {
            m_order = m_sourceOrder;
            setPositionForTrack(current);
        }
    }

    PlaybackMode mode() const noexcept { return m_mode; }
    const std::vector<qint64>& orderedTrackIds() const noexcept
    {
        return m_order;
    }
    const std::vector<qint64>& sourceTrackIds() const noexcept
    {
        return m_sourceOrder;
    }
    std::size_t position() const noexcept { return m_position; }

    bool selectTrack(qint64 trackId)
    {
        const auto track =
            std::find(m_order.begin(), m_order.end(), trackId);
        if (track == m_order.end()) {
            return false;
        }
        m_position =
            static_cast<std::size_t>(std::distance(m_order.begin(), track));
        return true;
    }

    bool insertNext(qint64 trackId)
    {
        if (trackId <= 0) {
            return false;
        }

        if (m_order.empty()) {
            m_order.push_back(trackId);
            m_sourceOrder.push_back(trackId);
            m_position = 0;
            return true;
        }

        const std::size_t insertPosition =
            std::min(m_position + 1, m_order.size());
        m_order.insert(
            m_order.begin() + static_cast<std::ptrdiff_t>(insertPosition),
            trackId);

        const auto current = currentTrackId();
        if (current) {
            const auto sourceCurrent =
                std::find(m_sourceOrder.begin(), m_sourceOrder.end(), *current);
            if (sourceCurrent != m_sourceOrder.end()) {
                const auto sourcePosition =
                    std::distance(m_sourceOrder.begin(), sourceCurrent) + 1;
                m_sourceOrder.insert(
                    m_sourceOrder.begin() + sourcePosition, trackId);
            } else {
                m_sourceOrder.push_back(trackId);
            }
        } else {
            m_sourceOrder.push_back(trackId);
        }
        return true;
    }

    std::optional<qint64> currentTrackId() const
    {
        if (m_order.empty() || m_position >= m_order.size()) {
            return std::nullopt;
        }
        return m_order[m_position];
    }

    std::optional<qint64> peekNext() const
    {
        if (m_order.empty() || m_mode == PlaybackMode::RepeatOne) {
            return std::nullopt;
        }
        if (m_position + 1 < m_order.size()) {
            return m_order[m_position + 1];
        }
        if (m_mode == PlaybackMode::RepeatAll) {
            return m_order.front();
        }
        return std::nullopt;
    }

    std::optional<qint64> next(bool naturalEnd = false)
    {
        if (m_order.empty()) {
            return std::nullopt;
        }
        if (naturalEnd && m_mode == PlaybackMode::RepeatOne) {
            return currentTrackId();
        }
        if (m_position + 1 < m_order.size()) {
            return m_order[++m_position];
        }
        if (m_mode == PlaybackMode::RepeatAll) {
            m_position = 0;
            return m_order[m_position];
        }
        return std::nullopt;
    }

    template <typename TryPlay>
    bool advanceToPlayable(bool naturalEnd, TryPlay&& tryPlay)
    {
        for (std::size_t attempt = 0; attempt < m_order.size(); ++attempt) {
            const auto candidate = next(attempt == 0 && naturalEnd);
            if (!candidate) {
                return false;
            }
            if (tryPlay(*candidate)) {
                return true;
            }
        }
        return false;
    }

    std::optional<qint64> previous()
    {
        if (m_order.empty()) {
            return std::nullopt;
        }
        if (m_position > 0) {
            return m_order[--m_position];
        }
        if (m_mode == PlaybackMode::RepeatAll) {
            m_position = m_order.size() - 1;
            return m_order[m_position];
        }
        return std::nullopt;
    }

private:
    void setPositionForTrack(qint64 trackId)
    {
        const auto current =
            std::find(m_order.begin(), m_order.end(), trackId);
        m_position = current == m_order.end()
            ? 0
            : static_cast<std::size_t>(
                  std::distance(m_order.begin(), current));
    }

    void shuffleFromCurrent(qint64 currentTrackId)
    {
        m_order = m_sourceOrder;
        const auto current =
            std::find(m_order.begin(), m_order.end(), currentTrackId);
        if (current != m_order.end()) {
            std::iter_swap(m_order.begin(), current);
        }
        if (m_order.size() > 2) {
            std::random_device device;
            std::mt19937 generator(device());
            std::shuffle(m_order.begin() + 1, m_order.end(), generator);
        }
        m_position = 0;
    }

    std::vector<qint64> m_order;
    std::vector<qint64> m_sourceOrder;
    std::size_t m_position = 0;
    PlaybackMode m_mode = PlaybackMode::Sequential;
};

} // namespace isac::library
