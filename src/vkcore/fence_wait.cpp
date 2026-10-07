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

namespace {

// The calling thread's high-resolution waitable timer (a plain one where Windows has none), closed with it.
HANDLE threadTimer() {
    struct Timer {
        HANDLE handle = nullptr;
        Timer() {
            handle = CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION,
                                            TIMER_ALL_ACCESS);
            if (!handle) {
                handle = CreateWaitableTimerExW(nullptr, nullptr, 0, TIMER_ALL_ACCESS);
            }
        }
        ~Timer() {
            if (handle) {
                CloseHandle(handle);
            }
        }
    };
    thread_local Timer timer;
    return timer.handle;
}

} // namespace

bool waitFenceFor(ID3D12Fence* fence, std::uint64_t value, HANDLE event, double seconds) {
    if (fence->GetCompletedValue() >= value) {
        return true;
    }
    HANDLE timer = threadTimer();
    if (!(seconds > 0.0 && seconds < 1.0) || !timer) { // also NaN; a longer wait is not what this is for
        return false;
    }
    LARGE_INTEGER due{};
    due.QuadPart = -static_cast<LONGLONG>(seconds * 1e7); // relative, in 100 ns units
    if (!SetWaitableTimerEx(timer, &due, 0, nullptr, nullptr, nullptr, 0)) {
        return false;
    }
    const HANDLE handles[] = {event, timer};
    while (fence->GetCompletedValue() < value) {
        if (FAILED(fence->SetEventOnCompletion(value, event)) ||
            WaitForMultipleObjects(2, handles, FALSE, INFINITE) != WAIT_OBJECT_0) {
            return fence->GetCompletedValue() >= value; // the timer ran out (or the wait failed)
        }
    }
    CancelWaitableTimer(timer);
    return true;
}

} // namespace evr::vkcore
