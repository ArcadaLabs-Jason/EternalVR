// The game's frame number (game_frame.hpp).

#include "vkcore/game_frame.hpp"

#include "vkcore/controllers_impl.hpp"
#include "vkcore/log.hpp"

#include <atomic>

namespace evr::vkcore::controllers::game_frame {

namespace {

constexpr const char* kTag = "game frame";

// idHands' render-model update: `mov rcx, [rip + disp]; add rcx, 0x125A60; call <getter>; mov rcx, [rip +
// disp]; cmp dword [rcx + 8], 0; jne +0xD; cmp eax, [r13 + 0xFC0]`.
constexpr const char* kFrameReadSignature = "48 8B 0D ?? ?? ?? ?? 48 81 C1 60 5A 12 00 E8 ?? ?? ?? ?? "
                                            "48 8B 0D ?? ?? ?? ?? 83 79 08 00 75 0D 41 3B 85 C0 0F 00 00";
// The getter the call lands on: `mov eax, [rcx + 8]; ret`.
constexpr const char* kGetterSignature = "8B 41 08 C3";
constexpr std::size_t kGetterCall = 14;
constexpr std::size_t kFrameOffset = 0x125A60 + 8;
constexpr std::size_t kHandsPlacedFrame = 0xFC0; // idHands: the frame UpdatePosition last ran in

std::atomic<const std::byte*> g_global{nullptr};

} // namespace

bool install(const GameImage& image) {
    if (g_global.load(std::memory_order_acquire)) {
        return true;
    }
    const std::byte* site = findUnique(image, kTag, "the hands' frame check", kFrameReadSignature);
    const std::byte* getter = site ? branchTarget(image, site + kGetterCall) : nullptr;
    const std::byte* global = site ? ripTarget(image, site + 3, site + 7) : nullptr;
    if (!site || !getter || !matchesAt(image, getter, kGetterSignature) || !global) {
        EVR_LOG("%s: the hands' frame check is not as expected; the frame is not read", kTag);
        return false;
    }
    g_global.store(global, std::memory_order_release);
    EVR_LOG("%s: the hands' frame check at RVA 0x%X, the frame's global at RVA 0x%X", kTag, image.rva(site),
            image.rva(global));
    return true;
}

std::optional<std::int32_t> now() {
    const std::byte* global = g_global.load(std::memory_order_acquire);
    const std::byte* game = nullptr;
    std::int32_t frame = 0;
    if (!global || !safeRead(global, game) || !game || !safeRead(game + kFrameOffset, frame)) {
        return std::nullopt;
    }
    return frame;
}

std::optional<std::int32_t> placedIn(const std::byte* hands) {
    std::int32_t frame = 0;
    if (!g_global.load(std::memory_order_acquire) || !hands || !safeRead(hands + kHandsPlacedFrame, frame)) {
        return std::nullopt;
    }
    return frame;
}

} // namespace evr::vkcore::controllers::game_frame
