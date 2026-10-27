#pragma once

#include <algorithm>
#include <cstdint>

namespace isac {

inline double linearFadeInGain(std::uint64_t frame,
                               std::uint64_t fadeFrames) noexcept
{
    if (fadeFrames == 0 || frame >= fadeFrames) {
        return 1.0;
    }
    return static_cast<double>(frame) /
           static_cast<double>(fadeFrames);
}

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

inline double playbackFadeOutGain(std::uint64_t frame,
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
    const std::uint64_t fadeOutStart = totalFrames - rampFrames;
    return frame > fadeOutStart
        ? static_cast<double>(totalFrames - frame) /
              static_cast<double>(rampFrames)
        : 1.0;
}

inline double overlapFadeGain(std::uint64_t frame,
                              std::uint64_t overlapStartFrame,
                              std::uint64_t overlapFrames,
                              bool nextTrack) noexcept
{
    if (overlapFrames == 0 || frame <= overlapStartFrame) {
        return nextTrack ? 0.0 : 1.0;
    }
    if (frame >= overlapStartFrame + overlapFrames) {
        return nextTrack ? 1.0 : 0.0;
    }

    const double progress =
        static_cast<double>(frame - overlapStartFrame) /
        static_cast<double>(overlapFrames);
    return nextTrack ? progress : 1.0 - progress;
}

} // namespace isac
