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
    if (m_decoderInitialized) {
        ma_decoder_uninit(&m_decoder);
        m_decoderInitialized = false;
    }
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
        m_deviceInitialized && ma_device_is_started(&m_device);
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
    const bool fadeEnabled = m_fadeEnabled.load(std::memory_order_relaxed);
    const ma_uint64 fadeFrames =
        m_fadeDurationFrames.load(std::memory_order_relaxed);
    const ma_uint64 totalFrames = m_totalFrames.load(std::memory_order_relaxed);

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
        const double* preDspChannels[] = {
            sourceChannels[0],
            sourceChannels[1]
        };
        m_preDspTap.write(preDspChannels, chunkFrames);
        const ma_uint64 blockStartFrame =
            m_renderedFrame.load(std::memory_order_relaxed);
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
        const double* postDspChannels[] = {
            finalBus.channel(0),
            finalBus.channel(1)
        };
        m_postDspTap.write(postDspChannels, chunkFrames);
        for (uint32_t i = 0; i < chunkFrames; ++i) {
            const ma_uint64 mediaFrame =
                blockStartFrame + std::min<std::size_t>(i, framesRead);
            const double gain = fadeEnabled
                ? playbackFadeGain(mediaFrame, totalFrames, fadeFrames)
                : 1.0;
            for (uint32_t ch = 0; ch < channels; ++ch) {
                currentOutPtr[i * channels + ch] =
                    ch < 2
                        ? static_cast<float>(finalBus.channel(ch)[i] * gain)
                        : 0.0f;
            }
        }

        currentOutPtr += chunkFrames * channels;
        framesRemaining -= chunkFrames;
    }
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
                (m_pcmRing.readableFrames() >=
                     static_cast<std::size_t>(m_outputSampleRate) *
                         PCM_PREBUFFER_MILLISECONDS / 1000 ||
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
            (m_pcmRing.readableFrames() >=
                 static_cast<std::size_t>(m_outputSampleRate) *
                     PCM_PREBUFFER_MILLISECONDS / 1000 ||
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

bool AudioEngine::loadAndPlay(const std::string& filePath, std::string* error) {
    if (error) {
        error->clear();
    }
    m_decoderLoaded.store(false, std::memory_order_release);
    destroyPlaybackDevice();
    stopDecoderThread();
    if (m_decoderInitialized) {
        ma_decoder_uninit(&m_decoder);
        m_decoderInitialized = false;
    }

    ma_decoder_config decoderConfig =
        ma_decoder_config_init(ma_format_f32, 2, 0);
    const ma_result decoderResult =
        ma_decoder_init_file(filePath.c_str(), &decoderConfig, &m_decoder);
    if (decoderResult != MA_SUCCESS) {
        if (error) {
            *error = "Unable to decode audio file (miniaudio error " +
                     std::to_string(decoderResult) + ")";
        }
        return false;
    }
    m_decoderInitialized = true;
    ma_uint32 decodedChannels = 0;
    if (ma_decoder_get_data_format(
            &m_decoder, nullptr, &decodedChannels, &m_outputSampleRate,
            nullptr, 0) != MA_SUCCESS ||
        m_outputSampleRate == 0 ||
        m_outputSampleRate > MAX_PLAYBACK_SAMPLE_RATE) {
        if (error) {
            *error = "The source sample rate is not supported (maximum 768 kHz)";
        }
        ma_decoder_uninit(&m_decoder);
        m_decoderInitialized = false;
        return false;
    }
    m_fadeDurationFrames.store(
        static_cast<ma_uint64>(
            m_fadeDurationSeconds * static_cast<double>(m_outputSampleRate)),
        std::memory_order_relaxed);
    m_pcmRing.reconfigure(m_outputSampleRate, 2);
    m_preDspTap.reset();
    m_postDspTap.reset();
    if (!rebuildGraph()) {
        if (error) {
            *error = "Unable to prepare the DSP graph for the source sample rate";
        }
        ma_decoder_uninit(&m_decoder);
        m_decoderInitialized = false;
        return false;
    }
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
    m_preDspTap.reset();
    m_postDspTap.reset();

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
        if (error) {
            *error = !prebufferReady
                ? "Timed out while preparing audio for playback"
                : "Audio decoding failed while preparing playback";
        }
        stopDecoderThread();
        ma_decoder_uninit(&m_decoder);
        m_decoderInitialized = false;
        return false;
    }

    std::string outputError;
    if (!initializePlaybackDevice(outputError)) {
        if (error) {
            *error = std::move(outputError);
        }
        stopDecoderThread();
        ma_decoder_uninit(&m_decoder);
        m_decoderInitialized = false;
        return false;
    }

    m_decoderLoaded.store(true, std::memory_order_release);
    if (ma_device_start(&m_device) != MA_SUCCESS) {
        if (error) {
            *error = "Unable to start audio output device";
        }
        m_decoderLoaded.store(false, std::memory_order_release);
        destroyPlaybackDevice();
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

bool AudioEngine::isPlaybackComplete() const noexcept
{
    return m_decoderLoaded.load(std::memory_order_acquire) &&
           m_decoderEof.load(std::memory_order_acquire) &&
           m_pcmRing.readableFrames() == 0;
}

void AudioEngine::seekToSeconds(double seconds) {
    if (!m_decoderLoaded.load() || !m_decoderInitialized) return;
    const double safeSeconds = std::isfinite(seconds) ? std::max(0.0, seconds) : 0.0;
    const auto requestedFrame = static_cast<ma_uint64>(
        safeSeconds * static_cast<double>(m_outputSampleRate));
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
    return static_cast<double>(m_renderedFrame.load(std::memory_order_acquire)) /
           static_cast<double>(m_outputSampleRate);
}

double AudioEngine::totalDurationSeconds() const {
    if (!m_decoderLoaded.load()) return 0.0;
    return static_cast<double>(m_totalFrames.load(std::memory_order_acquire)) /
           static_cast<double>(m_outputSampleRate);
}

} // namespace isac
