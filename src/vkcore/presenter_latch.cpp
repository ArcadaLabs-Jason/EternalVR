// The render latch and the view each present carries (docs/VR_HEAD_TRACKED.md): which game view the render
// thread latched, matched by its view axis against the camera hook's history (presenter_head.cpp).

#include "vkcore/presenter_impl.hpp"

#include "vkcore/view_slots.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace evr::vkcore {

void XrPresenter::Impl::onRenderLatch(const std::byte* renderView, const float* previousProjection) {
    const auto* axis = reinterpret_cast<const float*>(renderView + render_view::kViewAxis);
    std::uint64_t seq = 0;
    XrFovf fov{};
    std::uint64_t newest = 0;
    {
        std::lock_guard lock(historyMutex);
        newest = latestSeq;
        for (std::size_t i = 0; i < kHistorySize && i < newest; ++i) {
            const ViewRecord& record = history[(newest - i) % kHistorySize];
            if (std::memcmp(record.axis.data(), axis, sizeof(record.axis)) == 0) {
                seq = record.seq;
                fov = record.fov;
                break;
            }
        }
    }
    if (seq == 0) {
        latchUnmatched.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    latchMatched.fetch_add(1, std::memory_order_relaxed);
    latchedSeq.store(seq, std::memory_order_release);
    if (loggedLatches.fetch_add(1, std::memory_order_relaxed) < 12) {
        EVR_LOG("latch: render view %p latched view %llu (newest %llu)", static_cast<const void*>(renderView),
                static_cast<unsigned long long>(seq), static_cast<unsigned long long>(newest));
    }
    const ULONGLONG ticks = GetTickCount64();
    ULONGLONG last = lastLatchStatsTicks.load(std::memory_order_relaxed);
    if (ticks - last >= 10000 && lastLatchStatsTicks.compare_exchange_strong(last, ticks)) {
        // The projection left from this view's previous render against the FOV we asked for, to check
        // that the renderer uses fov_x / fov_y as given (a mismatch means the headset shows the image
        // at the wrong size).
        const float wantX = 1.0f / std::tan(fov.angleRight);
        const float wantY = 1.0f / std::tan(fov.angleUp);
        EVR_LOG("latch: %llu matched, %llu other view(s); previous projection [0][0] %.4f [1][1] %.4f [0][2] "
                "%.4f "
                "[1][2] %.4f [2][0] %.4f [2][1] %.4f; expected %.4f / %.4f",
                static_cast<unsigned long long>(latchMatched.load()),
                static_cast<unsigned long long>(latchUnmatched.load()), previousProjection[0],
                previousProjection[5], previousProjection[2], previousProjection[6], previousProjection[8],
                previousProjection[9], wantX, wantY);
    }
}

bool XrPresenter::Impl::latestView(ViewRecord& out, std::uint64_t& gap) {
    std::lock_guard lock(historyMutex);
    gap = 0;
    if (latestSeq == 0 || glory.flat()) {
        return false; // no view yet, or a glory kill on the flat screen
    }
    // The view the render thread latched most recently is the frame being presented, if the latch
    // hook matched one; otherwise the newest game view.
    std::uint64_t seq = latchedSeq.load(std::memory_order_acquire);
    if (seq == 0 || seq > latestSeq || latestSeq - seq >= kHistorySize) {
        seq = latestSeq;
    }
    const ViewRecord& record = history[seq % kHistorySize];
    if (record.seq != seq || qpcSeconds(qpcNow() - record.locatedQpc) > kViewStaleSeconds) {
        return false;
    }
    out = record;
    gap = latestSeq - seq;
    return true;
}

bool XrPresenter::Impl::viewForPresent(std::uint64_t pick, ViewRecord& out, std::uint64_t& gap) {
    if (!latestView(out, gap)) {
        return false;
    }
    // Parallel Eye: the view of the frame shown (eye 1's copy), not the newest, a frame off on many
    // presents (judder on head turns); ETERNALVR_TEST_PE_POSE=latest: the newest.
    if (pick != 0 && !parallelEyesSettings().latestPose) {
        const std::uint64_t newest = out.seq + gap;
        if (viewBySeq(pick, out)) {
            gap = newest - std::min(newest, out.seq);
        }
    }
    return true;
}

} // namespace evr::vkcore
