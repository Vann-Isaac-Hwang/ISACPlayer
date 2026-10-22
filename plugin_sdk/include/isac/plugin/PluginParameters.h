#pragma once

#include <cstddef>
#include <cstdint>

namespace isac {

enum class AudioParameterType : std::uint8_t {
    Continuous = 0,
    Boolean
};

struct AudioParameterDescriptor {
    const char* id = nullptr;
    const char* displayName = nullptr;
    const char* unit = nullptr;
    AudioParameterType type = AudioParameterType::Continuous;
    double minimum = 0.0;
    double maximum = 1.0;
    double defaultValue = 0.0;
};

// Optional metadata interface for hosts that want to generate a basic
// inspector without depending on a plugin's custom Qt widget.
class IAudioPluginParameters {
public:
    virtual ~IAudioPluginParameters() = default;
    virtual std::size_t parameterCount() const noexcept = 0;
    virtual const AudioParameterDescriptor* parameterAt(
        std::size_t index) const noexcept = 0;
};

} // namespace isac
