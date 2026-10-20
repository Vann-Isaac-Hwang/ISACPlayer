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
        m_order = std::move(trackIds);
        if (m_mode == PlaybackMode::Shuffle) {
            shuffleFromCurrent(currentTrackId);
        } else {
            const auto current =
                std::find(m_order.begin(), m_order.end(), currentTrackId);
            m_position = current == m_order.end()
                ? 0
                : static_cast<std::size_t>(std::distance(m_order.begin(), current));
        }
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
        }
    }

    PlaybackMode mode() const noexcept { return m_mode; }

    std::optional<qint64> currentTrackId() const
    {
        if (m_order.empty() || m_position >= m_order.size()) {
            return std::nullopt;
        }
        return m_order[m_position];
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
    void shuffleFromCurrent(qint64 currentTrackId)
    {
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
    std::size_t m_position = 0;
    PlaybackMode m_mode = PlaybackMode::Sequential;
};

} // namespace isac::library
