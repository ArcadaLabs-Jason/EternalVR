#pragma once

// Internal to adapter steering (adapter_steer.hpp), shared by adapter_steer.cpp (the game's devices: the
// decision, the order handed out, the hooks) and adapter_steer_probe.cpp (the headset's LUID from the
// runtime, docs/rig-findings/adapter-steer.md section 2).

#include "vkcore/adapter_order.hpp"

#include <optional>
#include <string>

namespace evr::vkcore::adapter_steer::detail {

// What the runtime said: the headset's LUID (or why there is none), the runtime's name, how long it took.
struct Probe {
    std::optional<adapter_order::Luid> luid;
    std::string runtime;
    std::string why; // empty when the LUID was read
    double ms = 0.0;
};

// The headset's LUID, asked once per process (adapter_steer.hpp): a short-lived OpenXR instance on this
// thread with the pass-through flag set; later calls return the first answer. adapter_steer.cpp calls it
// with its lock held.
Probe probeOnce();

} // namespace evr::vkcore::adapter_steer::detail
