#pragma once
#include "../../third_party/miniaudio/miniaudio.h"
#include "GraphCompiler.h"
#include "AudioTapBuffer.h"
#include "SpscAudioRingBuffer.h"
#include <array>
#include <cstdint>
#include <condition_variable>
#include <string>
#include <memory>
#include <mutex>
#include <atomic>
#include <thread>
#include <unordered_map>
#include <optional>
#include <vector>

namespace isac {

struct AudioOutputDevice {
    std::string name;
    std::array<std::uint8_t, sizeof(ma_device_id)> id{};
    bool useSystemDefault = false;
    bool isDefault = false;
    std::uint32_t maxChannels = 2;
};

constexpr int MAX_ENGINE_BLOCK_FRAMES = 8192; // 防御 PipeWire 大帧数回调
constexpr std::uint32_t MAX_PLAYBACK_SAMPLE_RATE = 768000;
constexpr std::uint32_t PCM_PREBUFFER_MILLISECONDS = 250;
constexpr std::size_t DECODER_CHUNK_FRAMES = 2048;

// 内部源节点：代表当前正在播放的音乐解码器 (0输入, 2输出: Out L, Out R)
class DecoderSourceNode : public IAudioNode {
public:
    std::string name() const override { return "Audio File Source"; }
    int numInputs() const override { return 0; }
    int numOutputs() const override { return 2; }
    std::string outputName(PortIndex p) const override { return p == 0 ? "PCM L" : "PCM R"; }

    AudioBuffer& internalBuffer() { return m_buffer; }

    void process(const AudioBuffer&, AudioBuffer& outBus, int frames) override {
        for (int ch = 0; ch < 2; ++ch) {
            std::copy_n(m_buffer.channel(ch), frames, outBus.channel(ch));
        }
    }
private:
    AudioBuffer m_buffer{2, MAX_ENGINE_BLOCK_FRAMES};
};

// 内部汇节点：代表声卡物理输出通道 (2输入: Speaker L, Speaker R, 0输出)
class MasterSinkNode : public IAudioNode {
public:
    std::string name() const override { return "Soundcard Output"; }
    int numInputs() const override { return 2; }
    int numOutputs() const override { return 0; }
    std::string inputName(PortIndex p) const override { return p == 0 ? "Speaker L" : "Speaker R"; }

    void process(const AudioBuffer& /*inBus*/, AudioBuffer& /*outBus*/, int /*frames*/) override {
        // Sink 节点的数据已经汇总在它的 inBus 中，无需再往 outBus 拷贝
    }
};

class AudioEngine {
public:
    AudioEngine(std::shared_ptr<IAudioNode> gainNode = {},
                std::shared_ptr<IAudioNode> addNode = {},
                std::shared_ptr<IAudioNode> equalizerNode = {});
    ~AudioEngine();

    bool loadAndPlay(const std::string& filePath, std::string* error = nullptr);
    std::uint32_t outputSampleRate() const noexcept { return m_outputSampleRate; }
    std::vector<AudioOutputDevice> availableOutputDevices();
    bool selectOutputDevice(const AudioOutputDevice& device,
                            std::string& error);
    std::optional<AudioOutputDevice> selectedOutputDevice() const;
    void setFadeEnabled(bool enabled) noexcept;
    void setFadeDurationSeconds(double seconds) noexcept;
    void setOverlapEnabled(bool enabled) noexcept;
    void setOverlapDurationSeconds(double seconds) noexcept;
    bool overlapEnabled() const noexcept;
    double overlapDurationSeconds() const noexcept;
    bool prepareNext(const std::string& filePath, std::string* error = nullptr);
    bool hasPreparedNext() const noexcept;
    bool startOverlap() noexcept;
    bool isOverlapActive() const noexcept;
    bool consumeOverlapTransition() noexcept;
    void cancelPreparedNext() noexcept;
    void setMasterGain(double factor) noexcept;
    double masterGain() const noexcept;
    void setPlaying(bool play);
    void servicePlaybackControl();
    bool isPlaying() const;
    bool isPlaybackComplete() const noexcept;
    void seekToSeconds(double seconds);
    double currentPositionSeconds() const;
    double totalDurationSeconds() const;
    ma_uint64 underrunFrames() const noexcept {
        return m_underrunFrames.load(std::memory_order_acquire);
    }
    std::size_t readPreDspSamples(double* left, double* right,
                                  std::size_t maxFrames) noexcept;
    std::size_t readPostDspSamples(double* left, double* right,
                                   std::size_t maxFrames) noexcept;

    // 图拓扑操作接口
    NodeId addNode(std::shared_ptr<IAudioNode> node);
    bool removeNode(NodeId id);
    bool hasNode(NodeId id) const { return m_nodes.find(id) != m_nodes.end(); }
    void clearConnections();
    bool connect(NodeId src, PortIndex srcPort, NodeId dst, PortIndex dstPort);
    bool disconnect(NodeId src, PortIndex srcPort, NodeId dst, PortIndex dstPort); // 断开单根线
    bool rebuildGraph();

    // 供 Patchbay 画布读取当前所有节点和连线
    const std::unordered_map<NodeId, std::shared_ptr<IAudioNode>>& nodes() const { return m_nodes; }
    const std::vector<Connection>& connections() const { return m_connections; }

    NodeId sourceNodeId() const { return m_sourceNode->id(); }
    NodeId sinkNodeId() const { return m_sinkNode->id(); }
    std::shared_ptr<IAudioNode> gainNode() const { return m_gainNode; }
    std::shared_ptr<IAudioNode> addNodePtr() const { return m_addNode; }
    std::shared_ptr<IAudioNode> equalizerNode() const { return m_equalizerNode; }

private:
    struct DecoderLane {
        DecoderLane()
            : pcmRing(48000, 2)
        {
        }

        ma_decoder decoder{};
        bool decoderInitialized = false;
        SpscAudioRingBuffer pcmRing;
        std::array<float, DECODER_CHUNK_FRAMES * 2> decodeBuffer{};
        std::atomic<bool> loaded{false};
        std::atomic<bool> ready{false};
        std::atomic<bool> failed{false};
        std::atomic<bool> eof{false};
        std::atomic<bool> skipFadeIn{false};
        std::atomic<ma_uint64> renderedFrame{0};
        std::atomic<ma_uint64> totalFrames{0};
    };

    static void dataCallback(ma_device* pDevice, void* pOutput, const void* pInput, ma_uint32 frameCount);
    void renderAudio(float* pInterleavedOutput, uint32_t frameCount, uint32_t channels) noexcept;
    void decoderLoop();
    void stopDecoderThread();
    void notifyDecoder();
    void markLaneReady(DecoderLane& lane);
    bool initializeLane(DecoderLane& lane, const std::string& filePath,
                        std::uint32_t requestedSampleRate,
                        std::string* error);
    void uninitializeLane(DecoderLane& lane);
    void clearAllLanes();
    bool initializePlaybackDevice(std::string& error);
    void destroyPlaybackDevice();

    ma_context m_context{};
    bool m_contextInitialized = false;
    ma_device m_device{};
    std::optional<AudioOutputDevice> m_selectedOutputDevice;
    std::uint32_t m_outputSampleRate = 48000;
    double m_fadeDurationSeconds = 2.0;
    double m_overlapDurationSeconds = 2.0;
    std::atomic<bool> m_decoderLoaded{false};
    bool m_deviceInitialized = false;

    NodeId m_nextNodeId = 1;
    std::unordered_map<NodeId, std::shared_ptr<IAudioNode>> m_nodes;
    std::vector<Connection> m_connections;

    std::shared_ptr<DecoderSourceNode> m_sourceNode;
    std::shared_ptr<MasterSinkNode> m_sinkNode;
    std::shared_ptr<IAudioNode> m_gainNode;
    std::shared_ptr<IAudioNode> m_addNode;
    std::shared_ptr<IAudioNode> m_equalizerNode;

    std::atomic<ExecutionPlan*> m_activePlan{nullptr};
    // rebuildGraph() stops the device before replacing this owner, so the
    // callback can use a raw plan pointer without refcounting or reclamation.
    std::shared_ptr<ExecutionPlan> m_activePlanOwner;
    std::array<DecoderLane, 2> m_decoderLanes;
    std::atomic<int> m_activeLane{0};
    std::atomic<int> m_preparedLane{-1};
    AudioTapBuffer m_preDspTap{65536, 2};
    AudioTapBuffer m_postDspTap{65536, 2};
    std::array<std::array<double, MAX_ENGINE_BLOCK_FRAMES>, 2> m_laneLeft{};
    std::array<std::array<double, MAX_ENGINE_BLOCK_FRAMES>, 2> m_laneRight{};
    std::array<std::array<float, MAX_ENGINE_BLOCK_FRAMES>, 2> m_lastOutput{};
    std::uint32_t m_lastOutputFrames = 0;
    std::atomic<bool> m_callbackActive{false};
    std::thread m_decoderThread;
    std::atomic<bool> m_decoderStop{false};
    std::mutex m_decoderMutex;
    std::mutex m_laneMutex;
    std::condition_variable m_decoderWake;
    std::condition_variable m_decoderReadyCondition;
    std::condition_variable m_seekCondition;

    // Control-thread requests are consumed by the decoder thread. The decoder
    // and all file I/O stay off the real-time audio callback.
    std::atomic<bool> m_seekPending{false};
    std::atomic<ma_uint64> m_pendingSeekFrame{0};
    std::atomic<bool> m_seekCompleted{false};
    std::atomic<ma_uint64> m_underrunFrames{0};
    std::atomic<bool> m_fadeEnabled{false};
    std::atomic<ma_uint64> m_fadeDurationFrames{96000};
    std::atomic<bool> m_overlapEnabled{false};
    std::atomic<ma_uint64> m_overlapDurationFrames{96000};
    std::atomic<bool> m_overlapActive{false};
    std::atomic<ma_uint64> m_activeOverlapFrames{0};
    std::atomic<ma_uint64> m_overlapElapsedFrames{0};
    std::atomic<bool> m_overlapTransitionPending{false};
    std::atomic<double> m_masterGain{1.0};
    std::atomic<bool> m_playbackRequested{false};
    std::atomic<bool> m_pauseFadeOutActive{false};
    std::atomic<bool> m_pauseFadeInActive{false};
    std::atomic<ma_uint64> m_pauseFadeOutElapsedFrames{0};
    std::atomic<ma_uint64> m_pauseFadeInElapsedFrames{0};
    std::atomic<bool> m_pauseFadeOutComplete{false};
};

static_assert(std::atomic<double>::is_always_lock_free,
              "Master gain must use a lock-free atomic");

} // namespace isac
