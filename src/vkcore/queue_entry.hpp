#pragma once

// The layer's queue entry points (queue_entry.cpp): every queue the device hands out is recorded with its
// family (DeviceData::queueFamilies), for the hooks that submit to it or time it.

#include "vkcore/dispatch.hpp"

namespace evr::vkcore {

// vkGetDeviceQueue and vkGetDeviceQueue2; nullptr for any other name.
PFN_vkVoidFunction findQueueHook(const char* name);

} // namespace evr::vkcore
