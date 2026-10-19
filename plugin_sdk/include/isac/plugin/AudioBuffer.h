#pragma once

#include <algorithm>
#include <cstdint>
#include <vector>

namespace isac {

constexpr int MAX_AUDIO_CHANNELS = 32;
using AudioSample = double;

class AudioBuffer {
public:
    AudioBuffer(int channels = 2, int frames = 512, uint32_t sampleRate = 48000) {
        resize(channels, frames, sampleRate);
    }

    void resize(int channels, int frames, uint32_t sampleRate = 48000) {
        m_numChannels = std::clamp(channels, 1, MAX_AUDIO_CHANNELS);
        m_numFrames = std::max(1, frames);
        m_sampleRate = sampleRate;
        m_storage.resize(static_cast<size_t>(m_numChannels) * m_numFrames, AudioSample{0.0});
    }

    void clear() {
        std::fill(m_storage.begin(), m_storage.end(), AudioSample{0.0});
    }

    AudioSample* channel(int ch) {
        const int safeCh = std::clamp(ch, 0, m_numChannels - 1);
        return m_storage.data() + static_cast<size_t>(safeCh) * m_numFrames;
    }

    const AudioSample* channel(int ch) const {
        const int safeCh = std::clamp(ch, 0, m_numChannels - 1);
        return m_storage.data() + static_cast<size_t>(safeCh) * m_numFrames;
    }

    int numChannels() const { return m_numChannels; }
    int numFrames() const { return m_numFrames; }
    uint32_t sampleRate() const { return m_sampleRate; }

private:
    int m_numChannels = 2;
    int m_numFrames = 512;
    uint32_t m_sampleRate = 48000;
    std::vector<AudioSample> m_storage;
};

using AudioBuffer64 = AudioBuffer;

} // namespace isac
