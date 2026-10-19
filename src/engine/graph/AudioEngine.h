#pragma once
#include "../../third_party/miniaudio/miniaudio.h"
#include "GraphCompiler.h"
#include <string>
#include <memory>
#include <atomic>
#include <unordered_map>

namespace isac {

constexpr int MAX_ENGINE_BLOCK_FRAMES = 8192; // 防御 PipeWire 大帧数回调

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

    bool loadAndPlay(const std::string& filePath);
    void setPlaying(bool play);
    bool isPlaying() const;
    void seekToSeconds(double seconds);
    double currentPositionSeconds() const;
    double totalDurationSeconds() const;

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
    static void dataCallback(ma_device* pDevice, void* pOutput, const void* pInput, ma_uint32 frameCount);
    void renderAudio(float* pInterleavedOutput, uint32_t frameCount, uint32_t channels);

    ma_decoder m_decoder{};
    ma_device m_device{};
    std::atomic<bool> m_decoderLoaded{false};

    NodeId m_nextNodeId = 1;
    std::unordered_map<NodeId, std::shared_ptr<IAudioNode>> m_nodes;
    std::vector<Connection> m_connections;

    std::shared_ptr<DecoderSourceNode> m_sourceNode;
    std::shared_ptr<MasterSinkNode> m_sinkNode;
    std::shared_ptr<IAudioNode> m_gainNode;
    std::shared_ptr<IAudioNode> m_addNode;
    std::shared_ptr<IAudioNode> m_equalizerNode;

    std::atomic<std::shared_ptr<ExecutionPlan>> m_activePlan{nullptr};
    // Keeps retired plans alive until the control thread observes that the
    // audio callback no longer holds its local shared_ptr.
    std::vector<std::shared_ptr<ExecutionPlan>> m_planHistory;
    std::vector<float> m_tempInterleaved;

    // Control-thread requests are consumed by the audio callback. The decoder
    // itself is only touched by the audio thread while the device is running.
    std::atomic<bool> m_seekPending{false};
    std::atomic<ma_uint64> m_pendingSeekFrame{0};
    std::atomic<ma_uint64> m_renderedFrame{0};
    std::atomic<ma_uint64> m_totalFrames{0};
};

} // namespace isac
