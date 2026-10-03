#include "vkcore/fence_wait.hpp"

namespace evr::vkcore {

bool waitFence(ID3D12Fence* fence, std::uint64_t value, HANDLE event, DWORD timeoutMs) {
    const ULONGLONG start = GetTickCount64();
    while (fence->GetCompletedValue() < value) {
        const ULONGLONG elapsed = GetTickCount64() - start;
        if (elapsed >= timeoutMs || FAILED(fence->SetEventOnCompletion(value, event))) {
            return false;
        }
        WaitForSingleObject(event, static_cast<DWORD>(timeoutMs - elapsed));
    }
    return true;
}

} // namespace evr::vkcore
