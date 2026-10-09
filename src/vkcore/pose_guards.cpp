// Tracked positions checked before use (pose_guards.hpp).

#include "vkcore/pose_guards.hpp"

#include "features/tracking/pose_guard.hpp"
#include "vkcore/log.hpp"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cwchar>
#include <string>

namespace evr::vkcore::pose_guards {

namespace {

constexpr auto kSlots = static_cast<std::size_t>(Slot::Count);

// Lines per family (the camera hook's head, the controllers'): holds, takes and releases; later ones are only
// counted.
constexpr std::uint32_t kMaxLines = 30;

struct SlotState {
    tracking::PoseGuard guard{tracking::kHandLimits};
    std::uint64_t epoch = 0;
    std::uint64_t holds = 0;
};

struct Family {
    const char* tag;
    std::atomic<std::uint32_t> lines{0};
};

std::array<Family, 2> g_families{{{"head"}, {"controllers"}}};
std::atomic<std::uint64_t> g_epoch{1};
std::atomic<XrTime> g_openUntil{0};

const char* slotName(Slot slot) {
    switch (slot) {
    case Slot::Head:
        return "head";
    case Slot::SyncHead:
        return "head (input)";
    case Slot::SyncAimLeft:
        return "left aim";
    case Slot::SyncAimRight:
        return "right aim";
    case Slot::SyncGripLeft:
        return "left grip";
    case Slot::SyncGripRight:
        return "right grip";
    case Slot::ViewAimLeft:
        return "left aim (game view)";
    case Slot::ViewAimRight:
        return "right aim (game view)";
    case Slot::ViewGripLeft:
        return "left grip (game view)";
    case Slot::ViewGripRight:
        return "right grip (game view)";
    case Slot::Count:
        break;
    }
    return "?";
}

bool isHead(Slot slot) {
    return slot == Slot::Head || slot == Slot::SyncHead;
}

std::array<SlotState, kSlots>& slots() {
    // Never destroyed: game threads can be inside a hook while the process exits.
    static auto* const s = [] {
        auto* out = new std::array<SlotState, kSlots>;
        (*out)[static_cast<std::size_t>(Slot::Head)].guard = tracking::PoseGuard(tracking::kHeadLimits);
        (*out)[static_cast<std::size_t>(Slot::SyncHead)].guard = tracking::PoseGuard(tracking::kHeadLimits);
        return out;
    }();
    return *s;
}

} // namespace

bool enabled() {
    static const bool on = [] {
        std::wstring value;
        const bool off =
            readEnv(L"ETERNALVR_POSE_GUARD", value) &&
            (value == L"0" || _wcsicmp(value.c_str(), L"false") == 0 || _wcsicmp(value.c_str(), L"off") == 0);
        if (off) {
            EVR_LOG("head: tracked positions are used as the runtime gives them (ETERNALVR_POSE_GUARD=0)");
        }
        return !off;
    }();
    return on;
}

namespace {

// Whether the family may log another line; the line after the last one says the rest are only counted.
bool mayLog(Family& family) {
    const std::uint32_t n = family.lines.fetch_add(1, std::memory_order_relaxed) + 1;
    if (n == kMaxLines + 1) {
        EVR_LOG("%s: %u tracked position lines logged; later ones are not", family.tag, kMaxLines);
    }
    return n <= kMaxLines;
}

} // namespace

bool check(Slot slot, Vec3& position, XrTime time) {
    if (slot >= Slot::Count || !enabled()) {
        return false;
    }
    SlotState& s = slots()[static_cast<std::size_t>(slot)];
    const std::uint64_t epoch = g_epoch.load(std::memory_order_acquire);
    if (s.epoch != epoch) {
        s.epoch = epoch;
        s.guard.reset();
    }
    if (time < g_openUntil.load(std::memory_order_acquire)) {
        s.guard.reset(); // the runtime's move is under way: every position is taken
    }
    const bool wasHolding = s.guard.holding();
    const tracking::GuardStep step = s.guard.update(position, static_cast<double>(time) * 1e-9);
    Family& family = g_families[isHead(slot) ? 0 : 1];
    const char* name = slotName(slot);
    if (step.held && !wasHolding) {
        ++s.holds;
        if (mayLog(family)) {
            EVR_LOG("%s: the tracked %s position jumped %.2f m in %.0f ms, further than a %s moves; the last "
                    "good one is held (hold %llu)",
                    family.tag, name, step.jumpMetres, step.sinceGoodSeconds * 1000.0,
                    isHead(slot) ? "head" : "hand", static_cast<unsigned long long>(s.holds));
        }
    } else if (step.taken && mayLog(family)) {
        EVR_LOG("%s: the tracked %s position stayed %.2f m from the last good one for %.2f s; taken as it is",
                family.tag, name, step.jumpMetres, step.heldSeconds);
    } else if (step.released && mayLog(family)) {
        EVR_LOG("%s: the tracked %s position is back within reach after %.0f ms held", family.tag, name,
                step.heldSeconds * 1000.0);
    }
    if (step.held) {
        position = step.position;
    }
    return step.held;
}

void resetAll(XrTime openUntil) {
    g_openUntil.store(openUntil, std::memory_order_release);
    g_epoch.fetch_add(1, std::memory_order_acq_rel);
}

} // namespace evr::vkcore::pose_guards
