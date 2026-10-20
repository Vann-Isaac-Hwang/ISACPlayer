#pragma once

#include <algorithm>
#include <cstdint>

namespace isac {

inline double playbackFadeGain(std::uint64_t frame,
                              std::uint64_t totalFrames,
                              std::uint64_t fadeFrames) noexcept
{
    if (totalFrames == 0 || fadeFrames == 0) {
        return 1.0;
    }
    if (frame >= totalFrames) {
        return 0.0;
    }

    const std::uint64_t rampFrames = std::min(fadeFrames, totalFrames);
    const double fadeIn = std::min(
        1.0, static_cast<double>(frame) / static_cast<double>(rampFrames));
    const std::uint64_t fadeOutStart = totalFrames - rampFrames;
    const double fadeOut = frame > fadeOutStart
        ? static_cast<double>(totalFrames - frame) /
              static_cast<double>(rampFrames)
        : 1.0;
    return fadeIn * fadeOut;
}

} // namespace isac
