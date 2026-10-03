#pragma once

// Waiting on a D3D12 fence with an event the presenter reuses.
//
// The events are auto-reset and shared by every wait of one presenter: a wait that timed out leaves its
// completion registered, so the event is set later by that older value and a following wait would wake on
// it before its own value is reached. A set event therefore proves nothing; the fence is read again after
// every wake.

#include <d3d12.h>
#include <windows.h>

#include <cstdint>

namespace evr::vkcore {

// True once `fence` has reached `value`, waiting at most `timeoutMs`; false on a timeout or when the event
// cannot be registered.
bool waitFence(ID3D12Fence* fence, std::uint64_t value, HANDLE event, DWORD timeoutMs);

} // namespace evr::vkcore
