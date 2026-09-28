// The game's menu cursor (menu_cursor.hpp, docs/rig-findings/menus.md).

#include "vkcore/menu_cursor.hpp"

#include "engine/eternal/resolver/pattern.hpp"
#include "vkcore/controllers_impl.hpp"
#include "vkcore/game_text.hpp"
#include "vkcore/log.hpp"
#include "vkcore/mp_guard.hpp"

#include <atomic>
#include <cstddef>
#include <mutex>

namespace evr::vkcore::menu_cursor {

namespace {

constexpr const char* kTag = "menu";

// idCursor::HandleEvent (RVA 0x18000E0 in build 25216728): on an SE_MOUSE event (type 3) it adds the
// event's dx and dy to mouseX (+0x10) and mouseY (+0x14), then clamps them to the render system's size.
constexpr const char* kHandleEventSignature =
    "40 53 48 83 EC 20 83 3A 03 48 8B D9 75 ?? 8B 42 04 01 41 10 8B 42 08 01 41 14";
// The common event loop's call of it: mov rcx,[idCursor pointer]; lea rdx,[rbp-0x20]; call HandleEvent
// (RVA 0x43E362, the pointer at RVA 0x47DDB88). Other calls share the shape; the one whose target is the
// handler above is the one.
constexpr const char* kCallSignature = "48 8B 0D ?? ?? ?? ?? 48 8D 55 E0 E8 ?? ?? ?? ??";

// idCursor (type info of the same build).
constexpr std::size_t kActive = 0x00;
constexpr std::size_t kMouseX = 0x10;
constexpr std::size_t kMouseY = 0x14;

std::once_flag g_once;
std::atomic<bool> g_installed{false};
std::atomic<const std::byte* const*> g_slot{nullptr};

bool locate() {
    GameImage image;
    if (!locateGameImage(image, kTag)) {
        EVR_LOG("%s: the game module cannot be read; menu pointer off", kTag);
        return false;
    }
    const std::byte* handler = findUnique(image, kTag, "cursor event handler", kHandleEventSignature);
    if (!handler) {
        EVR_LOG("%s: the cursor's event handler is not in this build; menu pointer off", kTag);
        return false;
    }
    const auto pattern = resolver::Pattern::parse(kCallSignature);
    if (!pattern) {
        return false;
    }
    const std::byte* slot = nullptr;
    int calls = 0;
    for (const std::size_t offset : resolver::findAll(image.text, *pattern)) {
        const std::byte* at = image.text.data() + offset;
        const std::byte* call = at + 11;
        if (call + 5 + readI32(call + 1) != handler) {
            continue;
        }
        ++calls;
        slot = ripTarget(image, at + 3, at + 7);
    }
    if (calls != 1 || !slot || !image.contains(slot, sizeof(void*))) {
        EVR_LOG(
            "%s: %d call(s) of the cursor's event handler through its pointer, expected 1; menu pointer off",
            kTag, calls);
        return false;
    }
    g_slot.store(reinterpret_cast<const std::byte* const*>(slot), std::memory_order_release);
    EVR_LOG("%s: cursor event handler at RVA 0x%X, cursor pointer at RVA 0x%X", kTag, image.rva(handler),
            image.rva(slot));
    return true;
}

} // namespace

bool install() {
    if (!mp_guard::allowsGameTouch()) {
        EVR_LOG("%s: menu pointer not installed: the multiplayer guard is %s", kTag,
                mp_policy::toString(mp_guard::state()));
        return false;
    }
    std::call_once(g_once, [] { g_installed.store(locate()); });
    return g_installed.load();
}

std::optional<CursorState> read() {
    const std::byte* const* slot = g_slot.load(std::memory_order_acquire);
    const std::byte* cursor = nullptr;
    if (!slot || !controllers::safeRead(reinterpret_cast<const std::byte*>(slot), cursor) || !cursor) {
        return std::nullopt;
    }
    std::uint8_t active = 0;
    CursorState state;
    if (!controllers::safeRead(cursor + kActive, active) ||
        !controllers::safeRead(cursor + kMouseX, state.x) ||
        !controllers::safeRead(cursor + kMouseY, state.y)) {
        return std::nullopt;
    }
    state.active = active != 0;
    return state;
}

} // namespace evr::vkcore::menu_cursor
