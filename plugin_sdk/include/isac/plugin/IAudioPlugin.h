#pragma once

#include "IAudioNode.h"
#include <memory>

namespace isac {

// Pure C++ plugin contract. Qt is deliberately absent from the DSP-facing
// interface so an external plugin can keep its processing code framework-free.
class IAudioPlugin {
public:
    virtual ~IAudioPlugin() = default;

    virtual const char* pluginId() const noexcept = 0;
    virtual const char* displayName() const noexcept = 0;
    virtual const char* description() const noexcept = 0;
    virtual std::shared_ptr<IAudioNode> createNode() const = 0;
};

} // namespace isac
