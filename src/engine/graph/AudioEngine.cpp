#define MINIAUDIO_IMPLEMENTATION
#include "AudioEngine.h"
#include "PlaybackFade.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>

namespace isac {

AudioEngine::AudioEngine(std::shared_ptr<IAudioNode> gainPluginNode,
                         std::shared_ptr<IAudioNode> addPluginNode,
                         std::shared_ptr<IAudioNode> equalizerPluginNode)
    : m_gainNode(std::move(gainPluginNode))
    , m_addNode(std::move(addPluginNode))
    , m_equalizerNode(std::move(equalizerPluginNode)) {
    m_contextInitialized =
        ma_context_init(nullptr, 0, nullptr, &m_context) == MA_SUCCESS;
    m_sourceNode = std::make_shared<DecoderSourceNode>();
    m_sinkNode   = std::make_shared<MasterSinkNode>();

    this->addNode(m_sourceNode);
    this->addNode(m_sinkNode);
    if (m_gainNode) {
        this->addNode(m_gainNode);
    }
    if (m_addNode) {
        this->addNode(m_addNode);
    }
    if (m_equalizerNode) {
        this->addNode(m_equalizerNode);
    }

    if (m_gainNode && m_equalizerNode) {
        connect(m_sourceNode->id(), 0, m_gainNode->id(), 0);
        connect(m_sourceNode->id(), 1, m_gainNode->id(), 1);
        connect(m_gainNode->id(), 0, m_equalizerNode->id(), 0);
        connect(m_gainNode->id(), 1, m_equalizerNode->id(), 1);
        connect(m_equalizerNode->id(), 0, m_sinkNode->id(), 0);
        connect(m_equalizerNode->id(), 1, m_sinkNode->id(), 1);
    } else if (m_gainNode) {
        connect(m_sourceNode->id(), 0, m_gainNode->id(), 0);
        connect(m_sourceNode->id(), 1, m_gainNode->id(), 1);
        connect(m_gainNode->id(), 0, m_sinkNode->id(), 0);
        connect(m_gainNode->id(), 1, m_sinkNode->id(), 1);
    } else if (m_equalizerNode) {
        connect(m_sourceNode->id(), 0, m_equalizerNode->id(), 0);
        connect(m_sourceNode->id(), 1, m_equalizerNode->id(), 1);
        connect(m_equalizerNode->id(), 0, m_sinkNode->id(), 0);
        connect(m_equalizerNode->id(), 1, m_sinkNode->id(), 1);
    } else {
        connect(m_sourceNode->id(), 0, m_sinkNode->id(), 0);
        connect(m_sourceNode->id(), 1, m_sinkNode->id(), 1);
    }
    rebuildGraph();
}

AudioEngine::~AudioEngine() {
    m_decoderLoaded.store(false, std::memory_order_release);
    destroyPlaybackDevice();
    stopDecoderThread();
    clearAllLanes();
    if (m_contextInitialized) {
        ma_context_uninit(&m_context);
        m_contextInitialized = false;
    }
}

std::vector<AudioOutputDevice> AudioEngine::availableOutputDevices()
{
    std::vector<AudioOutputDevice> devices;
    AudioOutputDevice systemDefault;
    systemDefault.name = "System Default";
    systemDefault.useSystemDefault = true;
    systemDefault.isDefault = true;
    devices.push_back(systemDefault);

    if (!m_contextInitialized) {
        return devices;
    }

    ma_device_info* playbackInfos = nullptr;
    ma_uint32 playbackCount = 0;
    if (ma_context_get_devices(
            &m_context, &playbackInfos, &playbackCount,
            nullptr, nullptr) != MA_SUCCESS) {
        return devices;
    }

    devices.reserve(static_cast<std::size_t>(playbackCount) + 1);
    for (ma_uint32 index = 0; index < playbackCount; ++index) {
        AudioOutputDevice device;
        device.name = playbackInfos[index].name;
        std::memcpy(device.id.data(), &playbackInfos[index].id,
                    sizeof(ma_device_id));
        device.isDefault = playbackInfos[index].isDefault == MA_TRUE;
        for (ma_uint32 formatIndex = 0;
             formatIndex < playbackInfos[index].nativeDataFormatCount;
             ++formatIndex) {
            device.maxChannels = std::max(
                device.maxChannels,
                playbackInfos[index].nativeDataFormats[formatIndex].channels);
        }
        devices.push_back(std::move(device));
    }
    return devices;
}

std::optional<AudioOutputDevice> AudioEngine::selectedOutputDevice() const
{
    return m_selectedOutputDevice;
}

bool AudioEngine::selectOutputDevice(const AudioOutputDevice& device,
                                     std::string& error)
{
    const bool wasStarted =
        m_deviceInitialized &&
        ma_device_is_started(&m_device) &&
        m_playbackRequested.load(std::memory_order_acquire);
    destroyPlaybackDevice();

    const auto previousSelection = m_selectedOutputDevice;
    m_selectedOutputDevice = device.useSystemDefault
        ? std::nullopt
        : std::optional<AudioOutputDevice>(device);

    if (!m_decoderLoaded.load(std::memory_order_acquire)) {
        return true;
    }

    if (initializePlaybackDevice(error)) {
        if (!wasStarted || ma_device_start(&m_device) == MA_SUCCESS) {
            return true;
        }
        error = "Unable to start the selected audio output device";
        destroyPlaybackDevice();
    }

    m_selectedOutputDevice = previousSelection;
    std::string restoreError;
    if (initializePlaybackDevice(restoreError)) {
        if (wasStarted && ma_device_start(&m_device) != MA_SUCCESS) {
            restoreError = "Unable to restart the previous audio output device";
            destroyPlaybackDevice();
        }
    }
    if (!restoreError.empty()) {
        error += "; previous output could not be restored: " + restoreError;
    }
    return false;
}

bool AudioEngine::initializePlaybackDevice(std::string& error)
{
    if (!m_contextInitialized) {
        error = "Audio backend context is unavailable";
        return false;
    }

    ma_device_config deviceConfig =
        ma_device_config_init(ma_device_type_playback);
    deviceConfig.playback.format = ma_format_f32;
    deviceConfig.playback.channels = 2;
    deviceConfig.sampleRate = m_outputSampleRate;
    deviceConfig.periodSizeInMilliseconds = 10;
    deviceConfig.periods = 3;
    deviceConfig.dataCallback = dataCallback;
    deviceConfig.pUserData = this;
    ma_device_id selectedDeviceId{};
    if (m_selectedOutputDevice) {
        std::memcpy(&selectedDeviceId, m_selectedOutputDevice->id.data(),
                    sizeof(selectedDeviceId));
        deviceConfig.playback.pDeviceID = &selectedDeviceId;
    }

    const ma_result result =
        ma_device_init(&m_context, &deviceConfig, &m_device);
    if (result != MA_SUCCESS) {
        error = "Unable to initialize audio output (miniaudio error " +
                std::to_string(result) + ")";
        return false;
    }
    m_deviceInitialized = true;
    return true;
}

void AudioEngine::destroyPlaybackDevice()
{
    if (!m_deviceInitialized) {
        return;
    }
    if (ma_device_is_started(&m_device)) {
        ma_device_stop(&m_device);
    }
    ma_device_uninit(&m_device);
    m_deviceInitialized = false;
}

NodeId AudioEngine::addNode(std::shared_ptr<IAudioNode> node) {
    NodeId id = m_nextNodeId++;
    node->setId(id);
    m_nodes[id] = std::move(node);
    return id;
}

bool AudioEngine::removeNode(NodeId id) {
    // Source and sink are engine-owned endpoints and must always remain.
    if (id == m_sourceNode->id() || id == m_sinkNode->id()) {
        return false;
    }

    if (!m_nodes.contains(id)) {
        return false;
    }

    m_connections.erase(
        std::remove_if(
            m_connections.begin(),
            m_connections.end(),
            [id](const Connection& connection) {
                return connection.srcNode == id || connection.dstNode == id;
            }),
        m_connections.end());

    if (m_gainNode && m_gainNode->id() == id) {
        m_gainNode.reset();
    }
    if (m_addNode && m_addNode->id() == id) {
        m_addNode.reset();
    }
    if (m_equalizerNode && m_equalizerNode->id() == id) {
        m_equalizerNode.reset();
    }
    m_nodes.erase(id);
    return true;
}

void AudioEngine::clearConnections() {
    m_connections.clear();
}

bool AudioEngine::connect(NodeId src, PortIndex srcPort, NodeId dst, PortIndex dstPort) {
    Connection c{src, srcPort, dst, dstPort};
    if (std::find(m_connections.begin(), m_connections.end(), c) == m_connections.end()) {
        m_connections.push_back(c);
    }
    return true;
}

bool AudioEngine::disconnect(NodeId src, PortIndex srcPort, NodeId dst, PortIndex dstPort) {
    Connection c{src, srcPort, dst, dstPort};
    auto it = std::remove(m_connections.begin(), m_connections.end(), c);
    if (it != m_connections.end()) {
        m_connections.erase(it, m_connections.end());
        return true;
    }
    return false;
}

bool AudioEngine::rebuildGraph() {
    auto newPlan = GraphCompiler::compile(
        m_nodes, m_connections, m_sinkNode->id(), MAX_ENGINE_BLOCK_FRAMES,
        m_outputSampleRate);
    if (!newPlan) return false;

    const bool wasPlaying =
        m_deviceInitialized &&
        ma_device_is_started(&m_device) &&
        m_playbackRequested.load(std::memory_order_acquire);
    if (wasPlaying) {
        ma_device_stop(&m_device);
    }
    m_activePlanOwner = std::move(newPlan);
    m_activePlan.store(m_activePlanOwner.get(), std::memory_order_release);
    m_activePlanSampleRate = m_outputSampleRate;
    if (wasPlaying) {
        ma_device_start(&m_device);
    }
    return true;
}

void AudioEngine::dataCallback(ma_device* pDevice, void* pOutput, const void*, ma_uint32 frameCount) {
    auto* engine = static_cast<AudioEngine*>(pDevice->pUserData);
    if (engine) {
        engine->renderAudio(static_cast<float*>(pOutput), frameCount, pDevice->playback.channels);
    }
}

void AudioEngine::renderAudio(float* pInterleavedOutput, uint32_t frameCount,
                              uint32_t channels) noexcept {
    m_callbackActive.store(true, std::memory_order_release);
    const bool playbackRequested =
        m_playbackRequested.load(std::memory_order_seq_cst);
    // A stale fade flag must never affect a block after playback has been
    // requested again. This also makes the control/audio-thread handoff
    // robust when play is pressed during the fade-out window.
    const bool pauseFadeOut =
        !playbackRequested &&
        m_pauseFadeOutActive.load(std::memory_order_seq_cst);
    const bool pauseFadeOutComplete =
        !playbackRequested &&
        m_pauseFadeOutComplete.load(std::memory_order_acquire);
    if (!playbackRequested && !pauseFadeOut) {
        std::fill_n(pInterleavedOutput, frameCount * channels, 0.0f);
        m_callbackActive.store(false, std::memory_order_release);
        return;
    }
    if (!playbackRequested && pauseFadeOutComplete) {
        // The control thread will stop the device and restore the pause
        // position. Do not consume more decoder frames while it is pending.
        std::fill_n(pInterleavedOutput, frameCount * channels, 0.0f);
        m_callbackActive.store(false, std::memory_order_release);
        return;
    }

    ExecutionPlan* plan = m_activePlan.load(std::memory_order_acquire);
    if (!m_decoderLoaded.load() || !plan || plan->sinkStepIndex < 0) {
        std::fill_n(pInterleavedOutput, frameCount * channels, 0.0f);
        m_callbackActive.store(false, std::memory_order_release);
        return;
    }
    const int initialLaneIndex = m_activeLane.load(std::memory_order_acquire);
    const auto& initialLane =
        m_decoderLanes[static_cast<std::size_t>(initialLaneIndex)];
    if (playbackRequested &&
        (!initialLane.ready.load(std::memory_order_acquire) ||
         initialLane.failed.load(std::memory_order_acquire))) {
        std::fill_n(pInterleavedOutput, frameCount * channels, 0.0f);
        m_callbackActive.store(false, std::memory_order_release);
        return;
    }

    // 分块安全处理：防止声卡单次请求超过 MAX_ENGINE_BLOCK_FRAMES 导致越界
    uint32_t framesRemaining = frameCount;
    float* currentOutPtr = pInterleavedOutput;
    const bool fadeEnabled = m_fadeEnabled.load(std::memory_order_relaxed);
    const ma_uint64 fadeFrames =
        m_fadeDurationFrames.load(std::memory_order_relaxed);
    const double masterGain =
        m_masterGain.load(std::memory_order_relaxed);
    const ma_uint64 transitionFadeFrames =
        m_activeTransitionFadeFrames.load(std::memory_order_relaxed);
    const bool pauseFadeIn =
        m_pauseFadeInActive.load(std::memory_order_acquire);
    const ma_uint64 pauseFadeElapsed =
        m_pauseFadeInElapsedFrames.load(std::memory_order_relaxed);
    const ma_uint64 pauseFadeOutElapsed =
        m_pauseFadeOutElapsedFrames.load(std::memory_order_relaxed);

    if (!playbackRequested && pauseFadeOut) {
        bool expected = false;
        if (m_pauseFrameCaptured.compare_exchange_strong(
                expected, true, std::memory_order_acq_rel,
                std::memory_order_acquire)) {
            const int active = m_activeLane.load(std::memory_order_acquire);
            m_pauseRestoreFrame.store(
                m_decoderLanes[static_cast<std::size_t>(active)]
                    .renderedFrame.load(std::memory_order_acquire),
                std::memory_order_release);
        }
    }

    while (framesRemaining > 0) {
        uint32_t chunkFrames = std::min<uint32_t>(framesRemaining, MAX_ENGINE_BLOCK_FRAMES);

        const int activeLaneIndex = m_activeLane.load(std::memory_order_acquire);
        auto& activeLane = m_decoderLanes[static_cast<std::size_t>(activeLaneIndex)];
        const bool overlapActive =
            m_overlapActive.load(std::memory_order_acquire);
        const int preparedLaneIndex =
            m_preparedLane.load(std::memory_order_acquire);
        const bool hasPreparedLane =
            overlapActive && preparedLaneIndex >= 0 &&
            preparedLaneIndex != activeLaneIndex;

        // Read PCM produced by the decoder thread. A short read is silence-fill
        // and never blocks the audio callback.
        auto& srcBuf = m_sourceNode->internalBuffer();
        srcBuf.clearFrames(static_cast<int>(chunkFrames));
        double* activeChannels[] = {
            m_laneLeft[static_cast<std::size_t>(activeLaneIndex)].data(),
            m_laneRight[static_cast<std::size_t>(activeLaneIndex)].data()
        };
        std::fill_n(activeChannels[0], chunkFrames, 0.0);
        std::fill_n(activeChannels[1], chunkFrames, 0.0);
        const std::size_t framesRead = activeLane.pcmRing.readPlanar(
            activeChannels, static_cast<std::size_t>(chunkFrames));
        std::size_t preparedFramesRead = 0;
        if (hasPreparedLane) {
            auto& preparedLane =
                m_decoderLanes[static_cast<std::size_t>(preparedLaneIndex)];
            double* preparedChannels[] = {
                m_laneLeft[static_cast<std::size_t>(preparedLaneIndex)].data(),
                m_laneRight[static_cast<std::size_t>(preparedLaneIndex)].data()
            };
            std::fill_n(preparedChannels[0], chunkFrames, 0.0);
            std::fill_n(preparedChannels[1], chunkFrames, 0.0);
            preparedFramesRead = preparedLane.pcmRing.readPlanar(
                preparedChannels, static_cast<std::size_t>(chunkFrames));
        }
        const bool sourceUnderrun =
            framesRead < chunkFrames &&
            !activeLane.eof.load(std::memory_order_acquire) &&
            !hasPreparedLane;

        const ma_uint64 blockStartFrame =
            activeLane.renderedFrame.load(std::memory_order_relaxed);
        const ma_uint64 totalFrames =
            activeLane.totalFrames.load(std::memory_order_relaxed);
        const ma_uint64 overlapFrames =
            m_activeOverlapFrames.load(std::memory_order_relaxed);
        const ma_uint64 overlapElapsed =
            m_overlapElapsedFrames.load(std::memory_order_relaxed);
        for (uint32_t i = 0; i < chunkFrames; ++i) {
            const double oldGain = hasPreparedLane
                ? overlapFadeGain(overlapElapsed + i, 0, overlapFrames, false)
                : 1.0;
            const double newGain = hasPreparedLane
                ? overlapFadeGain(overlapElapsed + i, 0, overlapFrames, true)
                : 0.0;
            srcBuf.channel(0)[i] =
                activeChannels[0][i] * oldGain;
            srcBuf.channel(1)[i] =
                activeChannels[1][i] * oldGain;
            if (hasPreparedLane && i < preparedFramesRead) {
                srcBuf.channel(0)[i] +=
                    m_laneLeft[static_cast<std::size_t>(preparedLaneIndex)][i] *
                    newGain;
                srcBuf.channel(1)[i] +=
                    m_laneRight[static_cast<std::size_t>(preparedLaneIndex)][i] *
                    newGain;
            }
        }
        const double* preDspChannels[] = {
            srcBuf.channel(0),
            srcBuf.channel(1)
        };
        m_preDspTap.write(preDspChannels, chunkFrames);
        if (sourceUnderrun) {
            m_underrunFrames.fetch_add(
                static_cast<ma_uint64>(chunkFrames - framesRead),
                std::memory_order_relaxed);
        }
        activeLane.renderedFrame.fetch_add(static_cast<ma_uint64>(framesRead),
                                           std::memory_order_release);
        if (hasPreparedLane) {
            m_decoderLanes[static_cast<std::size_t>(preparedLaneIndex)]
                .renderedFrame.fetch_add(
                    static_cast<ma_uint64>(preparedFramesRead),
                    std::memory_order_release);
        }

        // 2. 按拓扑排序顺序依次执行所有节点
        plan->clearBuffers(static_cast<int>(chunkFrames));
        for (auto& step : plan->steps) {
            for (const auto& route : step.incomingRoutes) {
                const AudioSample* upstreamOut =
                    plan->steps[route.upstreamStepIndex].outBus.channel(route.srcPort);
                AudioSample* myIn = step.inBus.channel(route.dstPort);
                for (uint32_t i = 0; i < chunkFrames; ++i) {
                    myIn[i] += upstreamOut[i];
                }
            }

            step.node->process(step.inBus, step.outBus, static_cast<int>(chunkFrames));
        }

        // 3. 直接从 MasterSinkNode 的 inBus 取出汇聚好的 2 声道数据写入声卡！
        const auto& finalBus = plan->steps[plan->sinkStepIndex].inBus;
        const double* postDspChannels[] = {
            finalBus.channel(0),
            finalBus.channel(1)
        };
        m_postDspTap.write(postDspChannels, chunkFrames);
        const bool recoveringFromUnderrun =
            m_underrunRecoveryActive && framesRead > 0;
        const std::uint32_t recoveryElapsed =
            m_underrunRecoveryElapsedFrames;
        ma_uint64 nonFiniteSamples = 0;
        ma_uint64 overRangeSamples = 0;
        std::array<float, 2> recoveryAnchor{};
        if (recoveringFromUnderrun && m_lastOutputFrames > 0) {
            for (std::size_t channel = 0; channel < recoveryAnchor.size();
                 ++channel) {
                recoveryAnchor[channel] =
                    m_lastOutput[channel][m_lastOutputFrames - 1];
            }
        }
        for (uint32_t i = 0; i < chunkFrames; ++i) {
            double pauseGain = 1.0;
            if (pauseFadeIn) {
                pauseGain = linearFadeInGain(
                    pauseFadeElapsed + i, transitionFadeFrames);
            } else if (pauseFadeOut) {
                pauseGain = playbackFadeOutGain(
                    pauseFadeOutElapsed + i,
                    transitionFadeFrames,
                    transitionFadeFrames);
            }
            for (uint32_t ch = 0; ch < channels; ++ch) {
                double gain = 1.0;
                if (!hasPreparedLane && fadeEnabled) {
                    gain = activeLane.skipFadeIn.load(std::memory_order_relaxed)
                        ? playbackFadeOutGain(
                              blockStartFrame + std::min<std::size_t>(i, framesRead),
                              totalFrames, fadeFrames)
                        : playbackFadeGain(
                              blockStartFrame + std::min<std::size_t>(i, framesRead),
                              totalFrames, fadeFrames);
                }
                gain *= pauseGain;
                float outputSample = 0.0f;
                if (ch < 2) {
                    outputSample = static_cast<float>(
                        finalBus.channel(ch)[i] * masterGain * gain);
                    if (sourceUnderrun && i >= framesRead) {
                        const std::uint32_t gapFrame =
                            i - static_cast<std::uint32_t>(framesRead);
                        if (gapFrame < UNDERRUN_CROSSFADE_FRAMES) {
                            const float anchor =
                                framesRead > 0
                                ? static_cast<float>(
                                      finalBus.channel(ch)[framesRead - 1] *
                                      masterGain * gain)
                                : (m_lastOutputFrames > 0
                                       ? m_lastOutput[ch][m_lastOutputFrames - 1]
                                       : 0.0f);
                            const double progress =
                                static_cast<double>(gapFrame + 1) /
                                static_cast<double>(
                                    UNDERRUN_CROSSFADE_FRAMES);
                            outputSample = static_cast<float>(
                                static_cast<double>(anchor) * (1.0 - progress) +
                                static_cast<double>(outputSample) * progress);
                        }
                    } else if (recoveringFromUnderrun &&
                               i < framesRead) {
                        const std::uint32_t recoveryFrame =
                            recoveryElapsed + i;
                        const double progress = std::min(
                            1.0,
                            static_cast<double>(recoveryFrame) /
                                static_cast<double>(
                                    UNDERRUN_CROSSFADE_FRAMES));
                        outputSample = static_cast<float>(
                            static_cast<double>(recoveryAnchor[ch]) *
                                (1.0 - progress) +
                            static_cast<double>(outputSample) * progress);
                    }
                }
                if (!std::isfinite(outputSample)) {
                    outputSample = 0.0f;
                    ++nonFiniteSamples;
                } else if (std::abs(outputSample) > 1.0f) {
                    ++overRangeSamples;
                }
                currentOutPtr[i * channels + ch] = outputSample;
                if (ch < 2) {
                    m_lastOutput[ch][i] =
                        currentOutPtr[i * channels + ch];
                }
            }
        }
        if (nonFiniteSamples > 0) {
            m_nonFiniteOutputSamples.fetch_add(
                nonFiniteSamples, std::memory_order_relaxed);
        }
        if (overRangeSamples > 0) {
            m_overRangeOutputSamples.fetch_add(
                overRangeSamples, std::memory_order_relaxed);
        }
        m_lastOutputFrames = chunkFrames;
        if (sourceUnderrun) {
            m_underrunRecoveryActive = true;
            m_underrunRecoveryElapsedFrames = 0;
        } else if (m_underrunRecoveryActive && framesRead > 0) {
            m_underrunRecoveryElapsedFrames = std::min(
                UNDERRUN_CROSSFADE_FRAMES,
                m_underrunRecoveryElapsedFrames + chunkFrames);
            if (m_underrunRecoveryElapsedFrames >=
                UNDERRUN_CROSSFADE_FRAMES) {
                m_underrunRecoveryActive = false;
                m_underrunRecoveryElapsedFrames = 0;
            }
        }

        currentOutPtr += chunkFrames * channels;
        framesRemaining -= chunkFrames;

        if (hasPreparedLane) {
            const ma_uint64 elapsedAfterBlock =
                overlapElapsed + static_cast<ma_uint64>(chunkFrames);
            m_overlapElapsedFrames.store(
                elapsedAfterBlock, std::memory_order_release);
            if (elapsedAfterBlock >= overlapFrames) {
                m_activeLane.store(preparedLaneIndex,
                                   std::memory_order_release);
                m_preparedLane.store(-1, std::memory_order_release);
                m_overlapActive.store(false, std::memory_order_release);
                m_overlapElapsedFrames.store(0, std::memory_order_release);
                m_decoderLanes[static_cast<std::size_t>(preparedLaneIndex)]
                    .skipFadeIn.store(true, std::memory_order_release);
                // The control thread owns decoder destruction and will reuse
                // the old lane only after the transition has been observed.
                m_overlapTransitionPending.store(
                    true, std::memory_order_release);
            }
        }
    }
    if (pauseFadeIn) {
        const ma_uint64 elapsed =
            pauseFadeElapsed + static_cast<ma_uint64>(frameCount);
        m_pauseFadeInElapsedFrames.store(elapsed, std::memory_order_release);
        if (elapsed >= transitionFadeFrames) {
            m_pauseFadeInActive.store(false, std::memory_order_release);
            m_pauseFadeInElapsedFrames.store(0, std::memory_order_release);
        }
    }
    if (pauseFadeOut) {
        const ma_uint64 elapsed =
            pauseFadeOutElapsed + static_cast<ma_uint64>(frameCount);
        m_pauseFadeOutElapsedFrames.store(elapsed, std::memory_order_release);
        if (elapsed >= transitionFadeFrames) {
            m_pauseFadeOutComplete.store(true, std::memory_order_release);
        }
    }
    m_callbackActive.store(false, std::memory_order_release);
}

std::size_t AudioEngine::readPreDspSamples(
    double* left, double* right, std::size_t maxFrames) noexcept
{
    double* channels[] = {left, right};
    return m_preDspTap.readLatest(channels, maxFrames);
}

std::size_t AudioEngine::readPostDspSamples(
    double* left, double* right, std::size_t maxFrames) noexcept
{
    double* channels[] = {left, right};
    return m_postDspTap.readLatest(channels, maxFrames);
}

void AudioEngine::setFadeEnabled(bool enabled) noexcept
{
    m_fadeEnabled.store(enabled, std::memory_order_relaxed);
}

void AudioEngine::setFadeDurationSeconds(double seconds) noexcept
{
    if (!std::isfinite(seconds)) {
        return;
    }
    const double clampedSeconds = std::clamp(seconds, 0.05, 30.0);
    m_fadeDurationSeconds = clampedSeconds;
    m_fadeDurationFrames.store(
        static_cast<ma_uint64>(
            clampedSeconds * static_cast<double>(m_outputSampleRate)),
        std::memory_order_relaxed);
}

void AudioEngine::setPlayPauseFadeEnabled(bool enabled) noexcept
{
    m_playPauseFadeEnabled.store(enabled, std::memory_order_relaxed);
}

void AudioEngine::setPlayPauseFadeDurationSeconds(double seconds) noexcept
{
    if (!std::isfinite(seconds)) {
        return;
    }
    const double clampedSeconds = std::clamp(seconds, 0.05, 30.0);
    m_playPauseFadeDurationSeconds = clampedSeconds;
    m_playPauseFadeDurationFrames.store(
        static_cast<ma_uint64>(
            clampedSeconds * static_cast<double>(m_outputSampleRate)),
        std::memory_order_relaxed);
}

ma_uint64 AudioEngine::configuredPlayPauseFadeFrames() const noexcept
{
    return transitionFadeFrameCount(
        m_playPauseFadeEnabled.load(std::memory_order_relaxed),
        m_playPauseFadeDurationFrames.load(std::memory_order_relaxed),
        OUTPUT_TRANSITION_RAMP_FRAMES);
}

void AudioEngine::setOverlapEnabled(bool enabled) noexcept
{
    m_overlapEnabled.store(enabled, std::memory_order_relaxed);
    if (!enabled && !m_overlapActive.load(std::memory_order_acquire)) {
        cancelPreparedNext();
    }
}

void AudioEngine::setOverlapDurationSeconds(double seconds) noexcept
{
    if (!std::isfinite(seconds)) {
        return;
    }
    m_overlapDurationSeconds = std::clamp(seconds, 0.1, 10.0);
    m_overlapDurationFrames.store(
        static_cast<ma_uint64>(
            m_overlapDurationSeconds *
            static_cast<double>(m_outputSampleRate)),
        std::memory_order_relaxed);
}

bool AudioEngine::overlapEnabled() const noexcept
{
    return m_overlapEnabled.load(std::memory_order_relaxed);
}

double AudioEngine::overlapDurationSeconds() const noexcept
{
    return m_overlapDurationSeconds;
}

bool AudioEngine::prepareNext(const std::string& filePath, std::string* error)
{
    if (error) {
        error->clear();
    }
    if (!m_decoderLoaded.load(std::memory_order_acquire) ||
        filePath.empty() || m_overlapActive.load(std::memory_order_acquire)) {
        return false;
    }

    std::lock_guard lock(m_laneMutex);
    if (m_preparedLane.load(std::memory_order_acquire) >= 0) {
        return true;
    }
    const int active = m_activeLane.load(std::memory_order_acquire);
    const int prepared = 1 - active;
    auto& lane = m_decoderLanes[static_cast<std::size_t>(prepared)];
    while (m_callbackActive.load(std::memory_order_acquire)) {
        std::this_thread::yield();
    }
    uninitializeLane(lane);
    if (!initializeLane(lane, filePath, m_outputSampleRate, error)) {
        return false;
    }
    lane.skipFadeIn.store(true, std::memory_order_release);
    m_preparedLane.store(prepared, std::memory_order_release);
    notifyDecoder();
    return true;
}

bool AudioEngine::hasPreparedNext() const noexcept
{
    const int prepared = m_preparedLane.load(std::memory_order_acquire);
    return prepared >= 0 &&
           m_decoderLanes[static_cast<std::size_t>(prepared)].loaded.load(
               std::memory_order_acquire) &&
           m_decoderLanes[static_cast<std::size_t>(prepared)].ready.load(
               std::memory_order_acquire);
}

bool AudioEngine::startOverlap() noexcept
{
    const int prepared = m_preparedLane.load(std::memory_order_acquire);
    if (!hasPreparedNext() || prepared < 0 ||
        m_overlapActive.load(std::memory_order_acquire)) {
        return false;
    }
    const int active = m_activeLane.load(std::memory_order_acquire);
    auto& lane = m_decoderLanes[static_cast<std::size_t>(active)];
    const ma_uint64 total = lane.totalFrames.load(std::memory_order_acquire);
    const ma_uint64 duration = std::min(
        m_overlapDurationFrames.load(std::memory_order_relaxed), total);
    if (duration == 0 || lane.renderedFrame.load(
            std::memory_order_acquire) >= total) {
        return false;
    }
    const ma_uint64 current = lane.renderedFrame.load(std::memory_order_acquire);
    const ma_uint64 effectiveDuration = std::min(duration, total - current);
    m_activeOverlapFrames.store(effectiveDuration, std::memory_order_release);
    m_overlapElapsedFrames.store(0, std::memory_order_release);
    m_overlapActive.store(true, std::memory_order_release);
    notifyDecoder();
    return true;
}

bool AudioEngine::isOverlapActive() const noexcept
{
    return m_overlapActive.load(std::memory_order_acquire);
}

bool AudioEngine::consumeOverlapTransition() noexcept
{
    return m_overlapTransitionPending.exchange(false,
                                                std::memory_order_acq_rel);
}

void AudioEngine::cancelPreparedNext() noexcept
{
    const int prepared = m_preparedLane.exchange(-1, std::memory_order_acq_rel);
    m_overlapActive.store(false, std::memory_order_release);
    m_activeOverlapFrames.store(0, std::memory_order_release);
    m_overlapElapsedFrames.store(0, std::memory_order_release);
    if (prepared < 0) {
        return;
    }
    // Destruction is deferred until the lane is reused or the engine stops.
    // The callback may already have captured this lane for its current block.
    m_decoderLanes[static_cast<std::size_t>(prepared)]
        .loaded.store(false, std::memory_order_release);
}

void AudioEngine::setMasterGain(double factor) noexcept
{
    if (!std::isfinite(factor)) {
        return;
    }
    m_masterGain.store(
        std::clamp(factor, 0.0, 4.0), std::memory_order_relaxed);
}

double AudioEngine::masterGain() const noexcept
{
    return m_masterGain.load(std::memory_order_relaxed);
}

void AudioEngine::decoderLoop()
{
    while (!m_decoderStop.load(std::memory_order_acquire)) {
        if (m_seekPending.exchange(false, std::memory_order_acq_rel)) {
            const ma_uint64 targetFrame =
                m_pendingSeekFrame.load(std::memory_order_acquire);
            const int active = m_activeLane.load(std::memory_order_acquire);
            auto& lane = m_decoderLanes[static_cast<std::size_t>(active)];
            {
                std::lock_guard lock(m_laneMutex);
                lane.pcmRing.reset();
                lane.eof.store(false, std::memory_order_release);
                lane.ready.store(false, std::memory_order_release);
                const ma_result result =
                    ma_decoder_seek_to_pcm_frame(&lane.decoder, targetFrame);
                if (result != MA_SUCCESS) {
                    lane.failed.store(true, std::memory_order_release);
                } else {
                    lane.renderedFrame.store(
                        targetFrame, std::memory_order_release);
                }
            }
            m_seekCompleted.store(true, std::memory_order_release);
            m_seekCondition.notify_all();
        }

        bool didWork = false;
        for (auto& lane : m_decoderLanes) {
            if (!lane.loaded.load(std::memory_order_acquire) ||
                lane.failed.load(std::memory_order_acquire) ||
                lane.eof.load(std::memory_order_acquire) ||
                lane.pcmRing.writableFrames() == 0) {
                if (lane.loaded.load(std::memory_order_acquire) &&
                    !lane.ready.load(std::memory_order_acquire) &&
                    (lane.pcmRing.readableFrames() >=
                         static_cast<std::size_t>(m_outputSampleRate) *
                             PCM_PREBUFFER_MILLISECONDS / 1000 ||
                     lane.eof.load(std::memory_order_acquire))) {
                    markLaneReady(lane);
                }
                continue;
            }

            const std::size_t framesToRead = std::min<std::size_t>(
                DECODER_CHUNK_FRAMES, lane.pcmRing.writableFrames());
            ma_uint64 framesRead = 0;
            ma_result result = MA_SUCCESS;
            {
                std::lock_guard lock(m_laneMutex);
                if (!lane.loaded.load(std::memory_order_acquire) ||
                    lane.failed.load(std::memory_order_acquire) ||
                    lane.eof.load(std::memory_order_acquire) ||
                    lane.pcmRing.writableFrames() == 0) {
                    continue;
                }
                result = ma_decoder_read_pcm_frames(
                    &lane.decoder, lane.decodeBuffer.data(), framesToRead,
                    &framesRead);
                if (framesRead > 0) {
                    lane.pcmRing.write(
                        lane.decodeBuffer.data(),
                        static_cast<std::size_t>(framesRead));
                }
            }
            didWork = true;

            if (result != MA_SUCCESS && result != MA_AT_END) {
                lane.failed.store(true, std::memory_order_release);
                markLaneReady(lane);
                continue;
            }

            if (framesRead < framesToRead || result == MA_AT_END) {
                lane.eof.store(true, std::memory_order_release);
            }

            if (lane.pcmRing.readableFrames() >=
                    static_cast<std::size_t>(m_outputSampleRate) *
                        PCM_PREBUFFER_MILLISECONDS / 1000 ||
                lane.eof.load(std::memory_order_acquire)) {
                markLaneReady(lane);
            }
        }

        if (!didWork) {
            std::unique_lock lock(m_decoderMutex);
            m_decoderWake.wait_for(
                lock, std::chrono::milliseconds(2), [this] {
                    return m_decoderStop.load(std::memory_order_acquire) ||
                           m_seekPending.load(std::memory_order_acquire);
                });
        }
    }
}

void AudioEngine::stopDecoderThread()
{
    m_decoderStop.store(true, std::memory_order_release);
    notifyDecoder();
    if (m_decoderThread.joinable()) {
        m_decoderThread.join();
    }
    m_decoderStop.store(false, std::memory_order_release);
}

void AudioEngine::notifyDecoder()
{
    m_decoderWake.notify_one();
}

void AudioEngine::markLaneReady(DecoderLane& lane)
{
    lane.ready.store(true, std::memory_order_release);
    m_decoderReadyCondition.notify_all();
}

bool AudioEngine::initializeLane(DecoderLane& lane,
                                 const std::string& filePath,
                                 std::uint32_t requestedSampleRate,
                                 std::string* error)
{
    ma_decoder_config decoderConfig =
        ma_decoder_config_init(ma_format_f32, 2, requestedSampleRate);
    const ma_result decoderResult =
        ma_decoder_init_file(filePath.c_str(), &decoderConfig, &lane.decoder);
    if (decoderResult != MA_SUCCESS) {
        if (error) {
            *error = "Unable to decode audio file (miniaudio error " +
                     std::to_string(decoderResult) + ")";
        }
        return false;
    }
    lane.decoderInitialized = true;

    ma_uint32 decodedChannels = 0;
    std::uint32_t sampleRate = 0;
    if (ma_decoder_get_data_format(
            &lane.decoder, nullptr, &decodedChannels, &sampleRate,
            nullptr, 0) != MA_SUCCESS ||
        sampleRate == 0 || sampleRate > MAX_PLAYBACK_SAMPLE_RATE) {
        if (error) {
            *error = "The source sample rate is not supported (maximum 768 kHz)";
        }
        uninitializeLane(lane);
        return false;
    }

    lane.pcmRing.reconfigure(sampleRate, 2);
    lane.ready.store(false, std::memory_order_release);
    lane.failed.store(false, std::memory_order_release);
    lane.eof.store(false, std::memory_order_release);
    lane.skipFadeIn.store(false, std::memory_order_release);
    lane.renderedFrame.store(0, std::memory_order_release);
    ma_uint64 totalFrames = 0;
    ma_decoder_get_length_in_pcm_frames(&lane.decoder, &totalFrames);
    lane.totalFrames.store(totalFrames, std::memory_order_release);
    lane.loaded.store(true, std::memory_order_release);
    return true;
}

void AudioEngine::uninitializeLane(DecoderLane& lane)
{
    lane.loaded.store(false, std::memory_order_release);
    lane.ready.store(false, std::memory_order_release);
    lane.failed.store(false, std::memory_order_release);
    lane.eof.store(true, std::memory_order_release);
    lane.pcmRing.reset();
    if (lane.decoderInitialized) {
        ma_decoder_uninit(&lane.decoder);
        lane.decoderInitialized = false;
    }
    lane.totalFrames.store(0, std::memory_order_release);
    lane.renderedFrame.store(0, std::memory_order_release);
}

void AudioEngine::clearAllLanes()
{
    std::lock_guard lock(m_laneMutex);
    for (auto& lane : m_decoderLanes) {
        uninitializeLane(lane);
    }
}

bool AudioEngine::loadAndPlay(const std::string& filePath, std::string* error) {
    if (error) {
        error->clear();
    }
    m_decoderLoaded.store(false, std::memory_order_release);
    m_playbackRequested.store(false, std::memory_order_release);
    m_pauseFadeOutActive.store(false, std::memory_order_release);
    m_pauseFadeInActive.store(false, std::memory_order_release);
    m_pauseFadeOutElapsedFrames.store(0, std::memory_order_release);
    m_pauseFadeInElapsedFrames.store(0, std::memory_order_release);
    m_pauseFadeOutComplete.store(false, std::memory_order_release);
    m_pauseFrameCaptured.store(false, std::memory_order_release);
    m_lastOutputFrames = 0;
    m_underrunRecoveryElapsedFrames = 0;
    m_underrunRecoveryActive = false;
    destroyPlaybackDevice();
    stopDecoderThread();
    clearAllLanes();
    m_activeLane.store(0, std::memory_order_release);
    m_preparedLane.store(-1, std::memory_order_release);
    m_overlapActive.store(false, std::memory_order_release);
    m_activeOverlapFrames.store(0, std::memory_order_release);
    m_overlapElapsedFrames.store(0, std::memory_order_release);
    m_overlapTransitionPending.store(false, std::memory_order_release);
    m_outputSampleRate = 48000;
    if (!initializeLane(m_decoderLanes[0], filePath, 0, error)) {
        return false;
    }
    m_outputSampleRate = m_decoderLanes[0].pcmRing.capacityFrames();
    m_fadeDurationFrames.store(
        static_cast<ma_uint64>(
            m_fadeDurationSeconds * static_cast<double>(m_outputSampleRate)),
        std::memory_order_relaxed);
    m_playPauseFadeDurationFrames.store(
        static_cast<ma_uint64>(
            m_playPauseFadeDurationSeconds *
            static_cast<double>(m_outputSampleRate)),
        std::memory_order_relaxed);
    m_overlapDurationFrames.store(
        static_cast<ma_uint64>(
            m_overlapDurationSeconds *
            static_cast<double>(m_outputSampleRate)),
        std::memory_order_relaxed);
    m_preDspTap.reset();
    m_postDspTap.reset();
    if (m_activePlanSampleRate != m_outputSampleRate &&
        !rebuildGraph()) {
        if (error) {
            *error = "Unable to prepare the DSP graph for the source sample rate";
        }
        clearAllLanes();
        return false;
    }
    m_seekPending.store(false, std::memory_order_release);
    m_seekCompleted.store(false, std::memory_order_release);
    m_underrunFrames.store(0, std::memory_order_release);
    m_nonFiniteOutputSamples.store(0, std::memory_order_release);
    m_overRangeOutputSamples.store(0, std::memory_order_release);
    m_preDspTap.reset();
    m_postDspTap.reset();

    m_decoderThread = std::thread(&AudioEngine::decoderLoop, this);
    std::string outputError;
    if (!initializePlaybackDevice(outputError)) {
        if (error) {
            *error = std::move(outputError);
        }
        stopDecoderThread();
        clearAllLanes();
        return false;
    }

    m_decoderLoaded.store(true, std::memory_order_release);
    m_activeTransitionFadeFrames.store(
        configuredPlayPauseFadeFrames(), std::memory_order_relaxed);
    m_pauseFadeInElapsedFrames.store(0, std::memory_order_release);
    m_pauseFadeInActive.store(true, std::memory_order_release);
    m_playbackRequested.store(true, std::memory_order_release);
    if (ma_device_start(&m_device) != MA_SUCCESS) {
        if (error) {
            *error = "Unable to start audio output device";
        }
        m_decoderLoaded.store(false, std::memory_order_release);
        m_playbackRequested.store(false, std::memory_order_release);
        destroyPlaybackDevice();
        stopDecoderThread();
        clearAllLanes();
        return false;
    }
    return true;
}

void AudioEngine::setPlaying(bool play) {
    if (!m_decoderLoaded.load(std::memory_order_acquire) ||
        !m_deviceInitialized) {
        return;
    }

    if (play) {
        const bool pausePositionNeedsRestore =
            m_pauseFrameCaptured.load(std::memory_order_acquire) ||
            m_pauseFadeOutActive.load(std::memory_order_acquire);
        if (pausePositionNeedsRestore) {
            m_playbackRequested.store(false, std::memory_order_seq_cst);
            m_pauseFadeOutActive.store(false, std::memory_order_seq_cst);
            m_pauseFadeOutComplete.store(false, std::memory_order_seq_cst);
            if (ma_device_is_started(&m_device)) {
                ma_device_stop(&m_device);
            }
            restorePausedPosition();
        }
        m_activeTransitionFadeFrames.store(
            configuredPlayPauseFadeFrames(), std::memory_order_relaxed);
        m_playbackRequested.store(true, std::memory_order_release);
        m_pauseFadeOutComplete.store(false, std::memory_order_release);
        m_pauseFadeInElapsedFrames.store(0, std::memory_order_release);
        m_pauseFadeInActive.store(true, std::memory_order_release);
        if (!ma_device_is_started(&m_device) &&
            ma_device_start(&m_device) != MA_SUCCESS) {
            m_playbackRequested.store(false, std::memory_order_release);
            m_pauseFadeInActive.store(false, std::memory_order_release);
        }
        return;
    }

    const int activeLaneIndex = m_activeLane.load(std::memory_order_acquire);
    const auto& activeLane =
        m_decoderLanes[static_cast<std::size_t>(activeLaneIndex)];
    if (!activeLane.ready.load(std::memory_order_acquire) ||
        activeLane.failed.load(std::memory_order_acquire)) {
        m_pauseFadeInActive.store(false, std::memory_order_release);
        m_pauseFadeOutActive.store(false, std::memory_order_seq_cst);
        m_pauseFadeInElapsedFrames.store(0, std::memory_order_release);
        m_pauseFadeOutComplete.store(false, std::memory_order_seq_cst);
        m_playbackRequested.store(false, std::memory_order_seq_cst);
        if (ma_device_is_started(&m_device)) {
            ma_device_stop(&m_device);
        }
        return;
    }

    m_pauseFadeInActive.store(false, std::memory_order_release);
    m_pauseFadeOutElapsedFrames.store(0, std::memory_order_release);
    m_pauseFadeInElapsedFrames.store(0, std::memory_order_release);
    if (!ma_device_is_started(&m_device)) {
        m_pauseFadeOutActive.store(false, std::memory_order_seq_cst);
        m_playbackRequested.store(false, std::memory_order_seq_cst);
        return;
    }
    if (m_deviceInitialized) {
        m_activeTransitionFadeFrames.store(
            configuredPlayPauseFadeFrames(), std::memory_order_relaxed);
        const int active = m_activeLane.load(std::memory_order_acquire);
        m_pauseRestoreFrame.store(
            m_decoderLanes[static_cast<std::size_t>(active)]
                .renderedFrame.load(std::memory_order_acquire),
            std::memory_order_release);
        m_pauseFrameCaptured.store(true, std::memory_order_release);
        m_pauseFadeOutComplete.store(false, std::memory_order_seq_cst);
        // Publish the fade state before withdrawing the play request. This
        // prevents the callback from outputting a silent block between the
        // two state changes, which would create a click on pause.
        m_pauseFadeOutActive.store(true, std::memory_order_seq_cst);
        m_playbackRequested.store(false, std::memory_order_seq_cst);
        return;
    }
    m_pauseFadeOutActive.store(false, std::memory_order_seq_cst);
    m_playbackRequested.store(false, std::memory_order_seq_cst);
    ma_device_stop(&m_device);
}

void AudioEngine::servicePlaybackControl()
{
    if (!m_pauseFadeOutComplete.exchange(
            false, std::memory_order_acq_rel) ||
        m_playbackRequested.load(std::memory_order_acquire) ||
        !m_deviceInitialized) {
        return;
    }
    m_pauseFadeOutActive.store(false, std::memory_order_release);
    if (ma_device_is_started(&m_device)) {
        ma_device_stop(&m_device);
    }
    restorePausedPosition();
}

bool AudioEngine::isPlaying() const {
    return m_decoderLoaded.load(std::memory_order_acquire) &&
           m_playbackRequested.load(std::memory_order_acquire);
}

bool AudioEngine::isPlaybackComplete() const noexcept
{
    const int active = m_activeLane.load(std::memory_order_acquire);
    const auto& lane = m_decoderLanes[static_cast<std::size_t>(active)];
    return m_decoderLoaded.load(std::memory_order_acquire) &&
           !m_overlapActive.load(std::memory_order_acquire) &&
           lane.eof.load(std::memory_order_acquire) &&
           lane.pcmRing.readableFrames() == 0;
}

bool AudioEngine::playbackDecodeFailed() const noexcept
{
    const int active = m_activeLane.load(std::memory_order_acquire);
    return m_decoderLoaded.load(std::memory_order_acquire) &&
           m_decoderLanes[static_cast<std::size_t>(active)]
               .failed.load(std::memory_order_acquire);
}

void AudioEngine::restorePausedPosition()
{
    if (!m_pauseFrameCaptured.exchange(false, std::memory_order_acq_rel) ||
        !m_decoderLoaded.load(std::memory_order_acquire)) {
        return;
    }
    seekToFrame(m_pauseRestoreFrame.load(std::memory_order_acquire));
}

void AudioEngine::seekToSeconds(double seconds) {
    if (!m_decoderLoaded.load()) return;
    const double safeSeconds = std::isfinite(seconds) ? std::max(0.0, seconds) : 0.0;
    seekToFrame(static_cast<ma_uint64>(
        safeSeconds * static_cast<double>(m_outputSampleRate)));
}

void AudioEngine::seekToFrame(ma_uint64 requestedFrame)
{
    if (!m_decoderLoaded.load()) return;
    const int active = m_activeLane.load(std::memory_order_acquire);
    const auto targetFrame = std::min(
        requestedFrame,
        m_decoderLanes[static_cast<std::size_t>(active)]
            .totalFrames.load(std::memory_order_acquire));

    const bool wasPlaying =
        m_deviceInitialized &&
        ma_device_is_started(&m_device) &&
        m_playbackRequested.load(std::memory_order_acquire);
    if (wasPlaying) {
        ma_device_stop(&m_device);
    }
    m_pauseFadeOutActive.store(false, std::memory_order_release);
    m_pauseFadeInActive.store(false, std::memory_order_release);
    m_pauseFadeOutElapsedFrames.store(0, std::memory_order_release);
    m_pauseFadeInElapsedFrames.store(0, std::memory_order_release);
    m_pauseFadeOutComplete.store(false, std::memory_order_release);
    m_pauseFrameCaptured.store(false, std::memory_order_release);

    cancelPreparedNext();
    m_decoderLanes[static_cast<std::size_t>(active)]
        .ready.store(false, std::memory_order_release);
    m_decoderLanes[static_cast<std::size_t>(active)]
        .failed.store(false, std::memory_order_release);
    m_decoderLanes[static_cast<std::size_t>(active)]
        .skipFadeIn.store(false, std::memory_order_release);
    m_seekCompleted.store(false, std::memory_order_release);
    m_pendingSeekFrame.store(targetFrame, std::memory_order_release);
    m_seekPending.store(true, std::memory_order_release);
    notifyDecoder();

    {
        std::unique_lock lock(m_decoderMutex);
        m_seekCondition.wait_for(
            lock, std::chrono::seconds(1), [this] {
                return m_seekCompleted.load(std::memory_order_acquire) ||
                       m_decoderLanes[static_cast<std::size_t>(
                           m_activeLane.load(std::memory_order_acquire))]
                           .failed.load(std::memory_order_acquire);
            });
        m_decoderReadyCondition.wait_for(
            lock, std::chrono::seconds(1), [this] {
                const auto& lane = m_decoderLanes[static_cast<std::size_t>(
                    m_activeLane.load(std::memory_order_acquire))];
                return lane.ready.load(std::memory_order_acquire) ||
                       lane.failed.load(std::memory_order_acquire);
            });
    }

    if (wasPlaying && m_decoderLoaded.load(std::memory_order_acquire) &&
        !m_decoderLanes[static_cast<std::size_t>(active)]
             .failed.load(std::memory_order_acquire)) {
        ma_device_start(&m_device);
    }
}

double AudioEngine::currentPositionSeconds() const {
    if (!m_decoderLoaded.load()) return 0.0;
    const int active = m_activeLane.load(std::memory_order_acquire);
    return static_cast<double>(
               m_decoderLanes[static_cast<std::size_t>(active)]
                   .renderedFrame.load(std::memory_order_acquire)) /
           static_cast<double>(m_outputSampleRate);
}

double AudioEngine::totalDurationSeconds() const {
    if (!m_decoderLoaded.load()) return 0.0;
    const int active = m_activeLane.load(std::memory_order_acquire);
    return static_cast<double>(
               m_decoderLanes[static_cast<std::size_t>(active)]
                   .totalFrames.load(std::memory_order_acquire)) /
           static_cast<double>(m_outputSampleRate);
}

} // namespace isac
