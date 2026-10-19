#pragma once

#include "AudioBuffer.h"
#include <cstdint>
#include <string>
#include <string_view>

namespace isac {

using NodeId = uint32_t;
using PortIndex = int;

struct Connection {
    NodeId srcNode;
    PortIndex srcPort;
    NodeId dstNode;
    PortIndex dstPort;

    bool operator==(const Connection& other) const {
        return srcNode == other.srcNode && srcPort == other.srcPort &&
               dstNode == other.dstNode && dstPort == other.dstPort;
    }
};

class IAudioNode {
public:
    virtual ~IAudioNode() = default;

    NodeId id() const { return m_id; }
    void setId(NodeId id) { m_id = id; }

    virtual std::string name() const = 0;
    virtual int numInputs() const = 0;
    virtual int numOutputs() const = 0;
    virtual std::string inputName(PortIndex port) const {
        return "In " + std::to_string(port);
    }
    virtual std::string outputName(PortIndex port) const {
        return "Out " + std::to_string(port);
    }

    virtual bool setParameter(std::string_view, double) noexcept { return false; }

    // This method is called by the real-time audio thread. It must not
    // allocate, lock, access I/O, or call into Qt.
    virtual void process(const AudioBuffer& inBus, AudioBuffer& outBus, int frames) = 0;

private:
    NodeId m_id = 0;
};

} // namespace isac
