#define MINIAUDIO_IMPLEMENTATION
#include "AudioEngine.h"
#include <algorithm>
#include <chrono>
#include <cmath>

namespace isac {

AudioEngine::AudioEngine(std::shared_ptr<IAudioNode> gainPluginNode,
                         std::shared_ptr<IAudioNode> addPluginNode,
                         std::shared_ptr<IAudioNode> equalizerPluginNode)
    : m_gainNode(std::move(gainPluginNode))
    , m_addNode(std::move(addPluginNode))
    , m_equalizerNode(std::move(equalizerPluginNode)) {
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
    if (m_deviceInitialized) {
        ma_device_stop(&m_device);
        ma_device_uninit(&m_device);
        m_deviceInitialized = false;
    }
    stopDecoderThread();
    if (m_decoderInitialized) {
        ma_decoder_uninit(&m_decoder);
        m_decoderInitialized = false;
    }
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
        m_nodes, m_connections, m_sinkNode->id(), MAX_ENGINE_BLOCK_FRAMES, 48000);
    if (!newPlan) return false;

    const bool wasPlaying = m_deviceInitialized && ma_device_is_started(&m_device);
    if (wasPlaying) {
        ma_device_stop(&m_device);
    }
    m_activePlanOwner = std::move(newPlan);
    m_activePlan.store(m_activePlanOwner.get(), std::memory_order_release);
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
    ExecutionPlan* plan = m_activePlan.load(std::memory_order_acquire);
    if (!m_decoderLoaded.load() || !plan || plan->sinkStepIndex < 0) {
        std::fill_n(pInterleavedOutput, frameCount * channels, 0.0f);
        return;
    }

    // 分块安全处理：防止声卡单次请求超过 MAX_ENGINE_BLOCK_FRAMES 导致越界
    uint32_t framesRemaining = frameCount;
    float* currentOutPtr = pInterleavedOutput;

    while (framesRemaining > 0) {
        uint32_t chunkFrames = std::min<uint32_t>(framesRemaining, MAX_ENGINE_BLOCK_FRAMES);

        // 1. Read PCM produced by the decoder thread. A short read is
        // silence-fill and never blocks the audio callback.
        auto& srcBuf = m_sourceNode->internalBuffer();
        srcBuf.clearFrames(static_cast<int>(chunkFrames));
        AudioSample* sourceChannels[] = {
            srcBuf.channel(0),
            srcBuf.channel(1)
        };
        const std::size_t framesRead = m_pcmRing.readPlanar(
            sourceChannels, static_cast<std::size_t>(chunkFrames));
        if (framesRead < chunkFrames &&
            !m_decoderEof.load(std::memory_order_acquire)) {
            m_underrunFrames.fetch_add(
                static_cast<ma_uint64>(chunkFrames - framesRead),
                std::memory_order_relaxed);
        }
        m_renderedFrame.fetch_add(static_cast<ma_uint64>(framesRead),
                                  std::memory_order_release);

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
        for (uint32_t i = 0; i < chunkFrames; ++i) {
            for (uint32_t ch = 0; ch < channels; ++ch) {
                currentOutPtr[i * channels + ch] =
                    ch < 2 ? static_cast<float>(finalBus.channel(ch)[i]) : 0.0f;
            }
        }

        currentOutPtr += chunkFrames * channels;
        framesRemaining -= chunkFrames;
    }
}

void AudioEngine::decoderLoop()
{
    bool readyForDevice = false;

    while (!m_decoderStop.load(std::memory_order_acquire)) {
        if (m_seekPending.exchange(false, std::memory_order_acq_rel)) {
            const ma_uint64 targetFrame =
                m_pendingSeekFrame.load(std::memory_order_acquire);

            // The device is stopped by seekToSeconds(), so the producer can
            // reset both cursors without racing the audio consumer.
            m_pcmRing.reset();
            m_decoderEof.store(false, std::memory_order_release);
            const ma_result result =
                ma_decoder_seek_to_pcm_frame(&m_decoder, targetFrame);
            if (result != MA_SUCCESS) {
                m_decoderFailed.store(true, std::memory_order_release);
            } else {
                m_renderedFrame.store(targetFrame, std::memory_order_release);
            }
            m_seekCompleted.store(true, std::memory_order_release);
            m_seekCondition.notify_all();
            readyForDevice = false;
        }

        if (m_decoderEof.load(std::memory_order_acquire) ||
            m_pcmRing.writableFrames() == 0) {
            if (!readyForDevice &&
                (m_pcmRing.readableFrames() >= PCM_PREBUFFER_FRAMES ||
                 m_decoderEof.load(std::memory_order_acquire))) {
                markDecoderReady();
                readyForDevice = true;
            }

            std::unique_lock lock(m_decoderMutex);
            m_decoderWake.wait_for(
                lock, std::chrono::milliseconds(2), [this] {
                    return m_decoderStop.load(std::memory_order_acquire) ||
                           m_seekPending.load(std::memory_order_acquire) ||
                           (!m_decoderEof.load(std::memory_order_acquire) &&
                            m_pcmRing.writableFrames() > 0);
                });
            continue;
        }

        const std::size_t framesToRead = std::min<std::size_t>(
            DECODER_CHUNK_FRAMES, m_pcmRing.writableFrames());
        ma_uint64 framesRead = 0;
        const ma_result result = ma_decoder_read_pcm_frames(
            &m_decoder, m_decodeBuffer.data(), framesToRead, &framesRead);

        if (framesRead > 0) {
            m_pcmRing.write(m_decodeBuffer.data(),
                            static_cast<std::size_t>(framesRead));
        }

        if (result != MA_SUCCESS && result != MA_AT_END) {
            m_decoderFailed.store(true, std::memory_order_release);
            markDecoderReady();
            break;
        }

        if (framesRead < framesToRead || result == MA_AT_END) {
            m_decoderEof.store(true, std::memory_order_release);
        }

        if (!readyForDevice &&
            (m_pcmRing.readableFrames() >= PCM_PREBUFFER_FRAMES ||
             m_decoderEof.load(std::memory_order_acquire))) {
            markDecoderReady();
            readyForDevice = true;
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

void AudioEngine::markDecoderReady()
{
    m_decoderReady.store(true, std::memory_order_release);
    m_decoderReadyCondition.notify_all();
}

bool AudioEngine::loadAndPlay(const std::string& filePath) {
    m_decoderLoaded.store(false, std::memory_order_release);
    if (m_deviceInitialized) {
        ma_device_stop(&m_device);
        ma_device_uninit(&m_device);
        m_deviceInitialized = false;
    }
    stopDecoderThread();
    if (m_decoderInitialized) {
        ma_decoder_uninit(&m_decoder);
        m_decoderInitialized = false;
    }

    ma_decoder_config decoderConfig = ma_decoder_config_init(ma_format_f32, 2, 48000);
    if (ma_decoder_init_file(filePath.c_str(), &decoderConfig, &m_decoder) != MA_SUCCESS) {
        return false;
    }
    m_decoderInitialized = true;
    ma_uint64 totalFrames = 0;
    ma_decoder_get_length_in_pcm_frames(&m_decoder, &totalFrames);
    m_totalFrames.store(totalFrames, std::memory_order_release);
    m_renderedFrame.store(0, std::memory_order_release);
    m_seekPending.store(false, std::memory_order_release);
    m_seekCompleted.store(false, std::memory_order_release);
    m_decoderEof.store(false, std::memory_order_release);
    m_decoderFailed.store(false, std::memory_order_release);
    m_decoderReady.store(false, std::memory_order_release);
    m_underrunFrames.store(0, std::memory_order_release);
    m_pcmRing.reset();

    m_decoderThread = std::thread(&AudioEngine::decoderLoop, this);
    bool prebufferReady = false;
    {
        std::unique_lock lock(m_decoderMutex);
        prebufferReady = m_decoderReadyCondition.wait_for(
            lock, std::chrono::seconds(2), [this] {
                return m_decoderReady.load(std::memory_order_acquire) ||
                       m_decoderFailed.load(std::memory_order_acquire);
            });
    }
    if (!prebufferReady || m_decoderFailed.load(std::memory_order_acquire)) {
        stopDecoderThread();
        ma_decoder_uninit(&m_decoder);
        m_decoderInitialized = false;
        return false;
    }

    ma_device_config deviceConfig = ma_device_config_init(ma_device_type_playback);
    deviceConfig.playback.format   = ma_format_f32;
    deviceConfig.playback.channels = 2; // 强制声卡开启 2 通道输出
    deviceConfig.sampleRate        = 48000;
    deviceConfig.periodSizeInFrames = 512;
    deviceConfig.periods = 3;
    deviceConfig.dataCallback      = dataCallback;
    deviceConfig.pUserData         = this;

    if (ma_device_init(nullptr, &deviceConfig, &m_device) != MA_SUCCESS) {
        stopDecoderThread();
        ma_decoder_uninit(&m_decoder);
        m_decoderInitialized = false;
        return false;
    }
    m_deviceInitialized = true;

    m_decoderLoaded.store(true, std::memory_order_release);
    if (ma_device_start(&m_device) != MA_SUCCESS) {
        m_decoderLoaded.store(false, std::memory_order_release);
        ma_device_uninit(&m_device);
        m_deviceInitialized = false;
        stopDecoderThread();
        ma_decoder_uninit(&m_decoder);
        m_decoderInitialized = false;
        return false;
    }
    return true;
}

void AudioEngine::setPlaying(bool play) {
    if (!m_decoderLoaded.load() || !m_deviceInitialized) return;
    if (play) ma_device_start(&m_device);
    else      ma_device_stop(&m_device);
}

bool AudioEngine::isPlaying() const {
    return m_decoderLoaded.load() && ma_device_is_started(&m_device);
}

void AudioEngine::seekToSeconds(double seconds) {
    if (!m_decoderLoaded.load() || !m_decoderInitialized) return;
    const double safeSeconds = std::isfinite(seconds) ? std::max(0.0, seconds) : 0.0;
    const auto requestedFrame = static_cast<ma_uint64>(safeSeconds * 48000.0);
    const auto targetFrame = std::min(requestedFrame, m_totalFrames.load(std::memory_order_acquire));

    const bool wasPlaying = m_deviceInitialized && ma_device_is_started(&m_device);
    if (wasPlaying) {
        ma_device_stop(&m_device);
    }

    m_decoderReady.store(false, std::memory_order_release);
    m_seekCompleted.store(false, std::memory_order_release);
    m_pendingSeekFrame.store(targetFrame, std::memory_order_release);
    m_seekPending.store(true, std::memory_order_release);
    notifyDecoder();

    {
        std::unique_lock lock(m_decoderMutex);
        m_seekCondition.wait_for(
            lock, std::chrono::seconds(1), [this] {
                return m_seekCompleted.load(std::memory_order_acquire) ||
                       m_decoderFailed.load(std::memory_order_acquire);
            });
        m_decoderReadyCondition.wait_for(
            lock, std::chrono::seconds(1), [this] {
                return m_decoderReady.load(std::memory_order_acquire) ||
                       m_decoderFailed.load(std::memory_order_acquire);
            });
    }

    if (wasPlaying && m_decoderLoaded.load(std::memory_order_acquire) &&
        !m_decoderFailed.load(std::memory_order_acquire)) {
        ma_device_start(&m_device);
    }
}

double AudioEngine::currentPositionSeconds() const {
    if (!m_decoderLoaded.load()) return 0.0;
    return static_cast<double>(m_renderedFrame.load(std::memory_order_acquire)) / 48000.0;
}

double AudioEngine::totalDurationSeconds() const {
    if (!m_decoderLoaded.load()) return 0.0;
    return static_cast<double>(m_totalFrames.load(std::memory_order_acquire)) / 48000.0;
}

} // namespace isac
