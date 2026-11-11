#pragma once

// The next layer's function for a hook called on a device its module keeps no record of: the module could not
// make the record (no memory, hook_guard.hpp) and stops handing its hooks out, but a hook handed out before
// can still be called for that device. Found through the layer's own record of the device (the loader's next
// vkGetDeviceProcAddr); null when the layer has none either, and the hook then does the safest no-op.

#include "vkcore/dispatch.hpp"
#include "vkcore/swapchain_entry.hpp"

namespace evr::vkcore {

// `dispatchable`: the device, or a queue or command buffer of it.
template <typename Fn, typename Handle>
Fn nextFunction(Handle dispatchable, const char* name) noexcept {
    const DeviceData* data = findDeviceData(reinterpret_cast<void*>(dispatchable));
    return data && data->nextGetDeviceProcAddr
               ? reinterpret_cast<Fn>(data->nextGetDeviceProcAddr(data->device, name))
               : nullptr;
}

} // namespace evr::vkcore
