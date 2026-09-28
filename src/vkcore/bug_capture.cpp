#include "vkcore/bug_capture.hpp"

#include "vkcore/log.hpp"

#include <atomic>

namespace evr::vkcore::bug_capture {

namespace {

std::atomic<bool> g_wanted{false};
std::atomic<LONGLONG> g_requestQpc{0};
std::atomic<std::uint32_t> g_taken{0};
std::atomic<bool> g_loggedLimit{false};
std::atomic<bool> g_loggedNoFolder{false};

LONGLONG now() {
    LARGE_INTEGER t;
    QueryPerformanceCounter(&t);
    return t.QuadPart;
}

} // namespace

void request() {
    if (g_taken.load() >= kMaxPerSession) {
        if (!g_loggedLimit.exchange(true)) {
            EVR_LOG("capture: %u captures taken this session; no more until the game restarts",
                    kMaxPerSession);
        }
        return;
    }
    if (g_wanted.load(std::memory_order_acquire)) {
        EVR_LOG("capture: asked for again before the last one was taken; one capture");
        return;
    }
    g_requestQpc.store(now(), std::memory_order_relaxed);
    g_wanted.store(true, std::memory_order_release);
    EVR_LOG("capture: asked for (the capture chord); the next eye pair is saved");
}

bool wanted() {
    return g_wanted.load(std::memory_order_relaxed);
}

double secondsWaiting() {
    if (!wanted()) {
        return 0.0;
    }
    LARGE_INTEGER f;
    QueryPerformanceFrequency(&f);
    return static_cast<double>(now() - g_requestQpc.load(std::memory_order_relaxed)) /
           static_cast<double>(f.QuadPart);
}

std::uint32_t take() {
    g_wanted.store(false, std::memory_order_release);
    return g_taken.fetch_add(1) + 1;
}

void retake() {
    g_taken.fetch_sub(1);
    g_wanted.store(true, std::memory_order_release);
    EVR_LOG("capture: the eye pair was given up; taking the next one");
}

LONGLONG requestQpc() {
    return g_requestQpc.load(std::memory_order_relaxed);
}

std::wstring directory() {
    const std::wstring log = logDirectory();
    if (log.empty()) {
        if (!g_loggedNoFolder.exchange(true)) {
            EVR_LOG("capture: ETERNALVR_LOG_DIR is not set; captures are not saved");
        }
        return {};
    }
    const std::wstring dir = log + L"\\captures";
    if (!CreateDirectoryW(dir.c_str(), nullptr) && GetLastError() != ERROR_ALREADY_EXISTS) {
        if (!g_loggedNoFolder.exchange(true)) {
            EVR_LOG("capture: cannot create the captures folder; captures are not saved");
        }
        return {};
    }
    return dir;
}

} // namespace evr::vkcore::bug_capture
