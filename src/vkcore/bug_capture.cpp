#include "vkcore/bug_capture.hpp"

#include "stereo_seq/seq_settings.hpp"
#include "vkcore/log.hpp"

#include <atomic>

namespace evr::vkcore::bug_capture {

namespace {

std::atomic<bool> g_wanted{false};
std::atomic<LONGLONG> g_requestQpc{0};
std::atomic<std::uint32_t> g_taken{0};
std::atomic<std::uint32_t> g_frames{0}; // counted against kMaxFramesPerSession
std::atomic<bool> g_loggedLimit{false};
std::atomic<bool> g_loggedNoFolder{false};

LONGLONG now() {
    LARGE_INTEGER t;
    QueryPerformanceCounter(&t);
    return t.QuadPart;
}

} // namespace

void request() {
    if (framesLeft() == 0) {
        if (!g_loggedLimit.exchange(true)) {
            EVR_LOG("capture: %u frames captured this session (a burst counts each of its frames); no more "
                    "until the game restarts",
                    kMaxFramesPerSession);
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

std::uint32_t take(std::uint32_t frames) {
    g_wanted.store(false, std::memory_order_release);
    g_frames.fetch_add(frames);
    return g_taken.fetch_add(1) + 1;
}

void retake(std::uint32_t frames) {
    g_taken.fetch_sub(1);
    giveBack(frames);
    g_wanted.store(true, std::memory_order_release);
    EVR_LOG("capture: the eye pair was given up; taking the next one");
}

std::uint32_t burstFrames() {
    static const std::uint32_t frames = [] {
        std::wstring text;
        if (!readEnv(L"ETERNALVR_CAPTURE_BURST", text) || text.empty()) {
            return 1u;
        }
        const auto n = stereo_seq::parseCaptureBurst(text);
        if (!n) {
            EVR_LOG("capture: ETERNALVR_CAPTURE_BURST is not 1 to %u; one frame per capture",
                    stereo_seq::kMaxCaptureBurst);
            return 1u;
        }
        EVR_LOG("capture: %u consecutive frame(s) per capture (ETERNALVR_CAPTURE_BURST)", *n);
        return *n;
    }();
    return frames;
}

void giveBack(std::uint32_t frames) {
    g_frames.fetch_sub(frames);
}

std::uint32_t framesLeft() {
    const std::uint32_t taken = g_frames.load();
    return taken >= kMaxFramesPerSession ? 0 : kMaxFramesPerSession - taken;
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
