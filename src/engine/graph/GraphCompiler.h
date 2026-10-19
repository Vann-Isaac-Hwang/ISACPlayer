#pragma once
#include "IAudioNode.h"
#include <memory>
#include <unordered_map>
#include <vector>

namespace isac {

// 编译后的单步指令
struct NodeExecutionStep {
    std::shared_ptr<IAudioNode> node;
    mutable AudioBuffer inBus;
    mutable AudioBuffer outBus;

    // 记录当前节点的每个输入端口，需要从哪些上游节点的输出端口拷贝/累加数据
    struct Route {
        int upstreamStepIndex; // 上游节点在 steps 数组中的下标
        PortIndex srcPort;
        PortIndex dstPort;
    };
    std::vector<Route> incomingRoutes;
};

// 只读执行计划：由 UI 线程编译生成，通过 shared_ptr 原子交换投递给音频线程
struct ExecutionPlan {
    std::vector<NodeExecutionStep> steps;
    int sinkStepIndex = -1; // 最终声卡输出节点所在的步骤下标

    void prepareBuffers(int maxFrames, uint32_t sampleRate) {
        for (auto& step : steps) {
            int inCh = std::max(1, step.node->numInputs());
            int outCh = std::max(1, step.node->numOutputs());
            step.inBus.resize(inCh, maxFrames, sampleRate);
            step.outBus.resize(outCh, maxFrames, sampleRate);
        }
    }
};

class GraphCompiler {
public:
    // 若存在环路返回 nullptr，成功则返回扁平化的实时执行计划
    static std::shared_ptr<ExecutionPlan> compile(
        const std::unordered_map<NodeId, std::shared_ptr<IAudioNode>>& nodes,
        const std::vector<Connection>& connections,
        NodeId masterSinkNodeId,
        int defaultBlockSize = 2048,
        uint32_t sampleRate = 48000);
};

} // namespace isac