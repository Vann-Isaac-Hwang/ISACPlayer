#define MINIAUDIO_IMPLEMENTATION
#include "AudioEngine.h"
#include <algorithm>
#include <cmath>

namespace isac {

AudioEngine::AudioEngine(std::shared_ptr<IAudioNode> gainPluginNode,
                         std::shared_ptr<IAudioNode> addPluginNode,
                         std::shared_ptr<IAudioNode> equalizerPluginNode)
    : m_tempInterleaved(MAX_ENGINE_BLOCK_FRAMES * 2, 0.0f)
    , m_gainNode(std::move(gainPluginNode))
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
    if (m_decoderLoaded.load()) {
        m_decoderLoaded.store(false);
        ma_device_stop(&m_device);
        ma_device_uninit(&m_device);
        ma_decoder_uninit(&m_decoder);
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

    m_activePlan.store(newPlan, std::memory_order_release);
    m_planHistory.push_back(newPlan);
    m_planHistory.erase(
        std::remove_if(
            m_planHistory.begin(),
            m_planHistory.end(),
            [&](const std::shared_ptr<ExecutionPlan>& plan) {
                return plan != newPlan && plan.use_count() == 1;
            }),
        m_planHistory.end());
    return true;
}

void AudioEngine::dataCallback(ma_device* pDevice, void* pOutput, const void*, ma_uint32 frameCount) {
    auto* engine = static_cast<AudioEngine*>(pDevice->pUserData);
    if (engine) {
        engine->renderAudio(static_cast<float*>(pOutput), frameCount, pDevice->playback.channels);
    }
}

void AudioEngine::renderAudio(float* pInterleavedOutput, uint32_t frameCount, uint32_t channels) {
    std::shared_ptr<ExecutionPlan> plan = m_activePlan.load(std::memory_order_acquire);
    if (!m_decoderLoaded.load() || !plan || plan->sinkStepIndex < 0) {
        std::fill_n(pInterleavedOutput, frameCount * channels, 0.0f);
        return;
    }

    // 分块安全处理：防止声卡单次请求超过 MAX_ENGINE_BLOCK_FRAMES 导致越界
    uint32_t framesRemaining = frameCount;
    float* currentOutPtr = pInterleavedOutput;

    while (framesRemaining > 0) {
        uint32_t chunkFrames = std::min<uint32_t>(framesRemaining, MAX_ENGINE_BLOCK_FRAMES);

        if (m_seekPending.exchange(false, std::memory_order_acq_rel)) {
            const ma_uint64 targetFrame = m_pendingSeekFrame.load(std::memory_order_acquire);
            ma_decoder_seek_to_pcm_frame(&m_decoder, targetFrame);
            m_renderedFrame.store(targetFrame, std::memory_order_release);
        }

        // 1. 解码音频帧并填入 SourceNode 内部缓冲
        ma_uint64 framesRead = 0;
        ma_decoder_read_pcm_frames(&m_decoder, m_tempInterleaved.data(), chunkFrames, &framesRead);

        auto& srcBuf = m_sourceNode->internalBuffer();
        srcBuf.clear();
        for (uint32_t i = 0; i < framesRead; ++i) {
            for (uint32_t ch = 0; ch < 2; ++ch) {
                srcBuf.channel(ch)[i] = static_cast<AudioSample>(
                    m_tempInterleaved[i * 2 + ch]);
            }
        }
        m_renderedFrame.fetch_add(framesRead, std::memory_order_release);

        // 2. 按拓扑排序顺序依次执行所有节点
        for (auto& step : plan->steps) {
            step.inBus.clear();
            step.outBus.clear();

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

bool AudioEngine::loadAndPlay(const std::string& filePath) {
    if (m_decoderLoaded.load()) {
        m_decoderLoaded.store(false);
        ma_device_stop(&m_device);
        ma_device_uninit(&m_device);
        ma_decoder_uninit(&m_decoder);
    }

    ma_decoder_config decoderConfig = ma_decoder_config_init(ma_format_f32, 2, 48000);
    if (ma_decoder_init_file(filePath.c_str(), &decoderConfig, &m_decoder) != MA_SUCCESS) {
        return false;
    }
    ma_uint64 totalFrames = 0;
    ma_decoder_get_length_in_pcm_frames(&m_decoder, &totalFrames);
    m_totalFrames.store(totalFrames, std::memory_order_release);
    m_renderedFrame.store(0, std::memory_order_release);
    m_seekPending.store(false, std::memory_order_release);

    ma_device_config deviceConfig = ma_device_config_init(ma_device_type_playback);
    deviceConfig.playback.format   = ma_format_f32;
    deviceConfig.playback.channels = 2; // 强制声卡开启 2 通道输出
    deviceConfig.sampleRate        = 48000;
    deviceConfig.dataCallback      = dataCallback;
    deviceConfig.pUserData         = this;

    if (ma_device_init(nullptr, &deviceConfig, &m_device) != MA_SUCCESS) {
        ma_decoder_uninit(&m_decoder);
        return false;
    }

    m_decoderLoaded.store(true);
    return ma_device_start(&m_device) == MA_SUCCESS;
}

void AudioEngine::setPlaying(bool play) {
    if (!m_decoderLoaded.load()) return;
    if (play) ma_device_start(&m_device);
    else      ma_device_stop(&m_device);
}

bool AudioEngine::isPlaying() const {
    return m_decoderLoaded.load() && ma_device_is_started(&m_device);
}

void AudioEngine::seekToSeconds(double seconds) {
    if (!m_decoderLoaded.load()) return;
    const double safeSeconds = std::isfinite(seconds) ? std::max(0.0, seconds) : 0.0;
    const auto requestedFrame = static_cast<ma_uint64>(safeSeconds * 48000.0);
    const auto targetFrame = std::min(requestedFrame, m_totalFrames.load(std::memory_order_acquire));
    m_pendingSeekFrame.store(targetFrame, std::memory_order_release);
    m_seekPending.store(true, std::memory_order_release);
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
