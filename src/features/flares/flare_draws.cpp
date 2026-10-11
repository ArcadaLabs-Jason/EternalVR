#include "features/flares/flare_draws.hpp"

namespace evr::flares {

namespace {

constexpr std::size_t kSlotBits = 12; // DrawShifts::kSlots == 1 << kSlotBits
static_assert(DrawShifts::kSlots == std::size_t{1} << kSlotBits);
constexpr std::size_t kMaxUsed = DrawShifts::kSlots * 3 / 4; // probes stay short

std::uint64_t pack(std::int32_t offset, std::int32_t shift) {
    return static_cast<std::uint32_t>(offset) | (std::uint64_t{static_cast<std::uint32_t>(shift)} << 32);
}

} // namespace

std::int32_t halfIndexShift(std::int32_t quads) {
    return quads * kIndexBytesPerQuad;
}

std::int64_t viewQuery(std::int64_t slot, int view) {
    return view == 1 && slot >= 0 && slot < kViewOneQueries ? slot + kViewOneQueries : slot;
}

bool queriesSplit(std::int64_t taken) {
    return taken >= 0 && taken <= kViewOneQueries;
}

CopyScan copyScan(int view, std::int32_t engineWords, std::int64_t taken) {
    constexpr std::int32_t kHalfWords = kPendingWords / 2;
    CopyScan scan;
    scan.wait = queriesSplit(taken);
    if (view == 1) {
        // Every word of the upper half: view 1's queries, and the engine's own slots up there when it took
        // more than the lower half (copied without the wait then).
        scan.firstWord = kHalfWords;
        scan.words = kHalfWords;
        scan.firstSlot = kViewOneQueries;
    } else {
        // The engine's scan: within the lower half while the split holds, else every word it took.
        scan.words = engineWords;
    }
    return scan;
}

std::size_t DrawShifts::home(std::uintptr_t geometry) {
    return static_cast<std::size_t>((std::uint64_t{geometry} >> 4) * 0x9E3779B97F4A7C15ull >>
                                    (64 - kSlotBits));
}

void DrawShifts::reset() {
    const std::size_t used = used_.load(std::memory_order_relaxed);
    used_.store(0, std::memory_order_release);
    for (std::size_t i = 0; i < used; ++i) {
        keys_[taken_[i]].store(0, std::memory_order_release);
    }
}

bool DrawShifts::put(std::uintptr_t geometry, std::int32_t offset, std::int32_t shift) {
    if (geometry == 0) {
        return false;
    }
    const std::size_t used = used_.load(std::memory_order_relaxed);
    for (std::size_t probe = 0, i = home(geometry); probe < kSlots; ++probe, i = (i + 1) % kSlots) {
        const std::uintptr_t key = keys_[i].load(std::memory_order_relaxed);
        if (key == geometry) {
            values_[i].store(pack(offset, shift), std::memory_order_release);
            return true;
        }
        if (key == 0) {
            if (used >= kMaxUsed) {
                return false;
            }
            values_[i].store(pack(offset, shift), std::memory_order_relaxed);
            keys_[i].store(geometry, std::memory_order_release);
            taken_[used] = static_cast<std::uint16_t>(i);
            used_.store(used + 1, std::memory_order_release);
            return true;
        }
    }
    return false;
}

std::optional<std::int32_t> DrawShifts::find(std::uintptr_t geometry, std::int32_t offset) const {
    if (geometry == 0 || empty()) {
        return std::nullopt;
    }
    for (std::size_t probe = 0, i = home(geometry); probe < kSlots; ++probe, i = (i + 1) % kSlots) {
        const std::uintptr_t key = keys_[i].load(std::memory_order_acquire);
        if (key == 0) {
            return std::nullopt;
        }
        if (key == geometry) {
            const std::uint64_t value = values_[i].load(std::memory_order_acquire);
            if (static_cast<std::int32_t>(static_cast<std::uint32_t>(value)) != offset) {
                return std::nullopt;
            }
            return static_cast<std::int32_t>(static_cast<std::uint32_t>(value >> 32));
        }
    }
    return std::nullopt;
}

bool drawsUnseen(const DrawCounts& counts) {
    return counts.rebuiltViewOne >= kUnseenRebuilds && counts.setups > 0 && counts.binds == 0;
}

} // namespace evr::flares
