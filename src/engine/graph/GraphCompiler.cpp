#include "GraphCompiler.h"
#include <queue>

namespace isac {

std::shared_ptr<ExecutionPlan> GraphCompiler::compile(
    const std::unordered_map<NodeId, std::shared_ptr<IAudioNode>>& nodes,
    const std::vector<Connection>& connections,
    NodeId masterSinkNodeId,
    int defaultBlockSize,
    uint32_t sampleRate)
{
    if (nodes.empty()) return nullptr;

    // 1. 计算每个节点的入度 (In-degree) 与邻接表
    std::unordered_map<NodeId, int> inDegree;
    std::unordered_map<NodeId, std::vector<NodeId>> adj;
    for (const auto& [id, _] : nodes) {
        inDegree[id] = 0;
    }

    for (const auto& conn : connections) {
        if (nodes.count(conn.srcNode) && nodes.count(conn.dstNode)) {
            adj[conn.srcNode].push_back(conn.dstNode);
            inDegree[conn.dstNode]++;
        }
    }

    // 2. Kahn 拓扑排序 + 环路检测
    std::queue<NodeId> q;
    for (const auto& [id, deg] : inDegree) {
        if (deg == 0) q.push(id);
    }

    std::vector<NodeId> sortedOrder;
    while (!q.empty()) {
        NodeId curr = q.front();
        q.pop();
        sortedOrder.push_back(curr);

        for (NodeId next : adj[curr]) {
            if (--inDegree[next] == 0) {
                q.push(next);
            }
        }
    }

    // 若排序后的节点数小于总节点数，说明图中存在环路 (Cycle)！拒绝编译
    if (sortedOrder.size() != nodes.size()) {
        return nullptr;
    }

    // 3. 构建线性执行计划 ExecutionPlan
    auto plan = std::make_shared<ExecutionPlan>();
    std::unordered_map<NodeId, int> nodeIdToStepIdx;

    for (size_t i = 0; i < sortedOrder.size(); ++i) {
        NodeId nid = sortedOrder[i];
        nodeIdToStepIdx[nid] = static_cast<int>(i);

        NodeExecutionStep step;
        step.node = nodes.at(nid);
        plan->steps.push_back(std::move(step));

        if (nid == masterSinkNodeId) {
            plan->sinkStepIndex = static_cast<int>(i);
        }
    }

    // 4. 绑定端口间的数据路由
    for (const auto& conn : connections) {
        if (!nodeIdToStepIdx.count(conn.srcNode) || !nodeIdToStepIdx.count(conn.dstNode))
            continue;

        int srcStep = nodeIdToStepIdx[conn.srcNode];
        int dstStep = nodeIdToStepIdx[conn.dstNode];

        auto& dstNode = plan->steps[dstStep].node;
        auto& srcNode = plan->steps[srcStep].node;

        if (conn.srcPort >= 0 && conn.srcPort < srcNode->numOutputs() &&
            conn.dstPort >= 0 && conn.dstPort < dstNode->numInputs()) {
            plan->steps[dstStep].incomingRoutes.push_back({srcStep, conn.srcPort, conn.dstPort});
        }
    }

    plan->prepareBuffers(defaultBlockSize, sampleRate);
    return plan;
}

} // namespace isac