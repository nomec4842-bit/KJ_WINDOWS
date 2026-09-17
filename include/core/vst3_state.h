#pragma once
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace kj {
struct Vst3PluginState {
    bool hasComponent = false, hasController = false;
    std::string component, controller; // Raw bytes; project I/O encodes these.
    std::vector<std::pair<std::uint32_t, double>> parameters;
};
struct Vst3ParameterTarget { std::uint32_t id = 0; std::string name; };
struct Vst3SlotState {
    std::string id, name, path, classId;
    bool instrument = false, bypass = false;
    Vst3PluginState state;
    std::vector<Vst3ParameterTarget> learnedParameters;
};
using Vst3RackState = std::vector<Vst3SlotState>; // Instrument first, then effects in order.
}
