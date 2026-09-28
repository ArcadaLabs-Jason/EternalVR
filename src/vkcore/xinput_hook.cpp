// Motion controllers, the virtual gamepad (controllers.hpp, docs/rig-findings/input-aim.md section 5, T-009
// L2).
//
// The exe imports XInputGetState from XINPUT1_3.dll by ordinal 2 only. Its import slot is replaced; the
// replacement calls the real function and, for pad 0 while the virtual gamepad is active, merges our
// input in (features/input/virtual_gamepad.hpp), so a connected pad keeps working. It runs on the game's
// pad sampler thread. Pad 0 reads as connected while the controllers are live; the packet number moves
// on whenever the merged state changes, which is when the game reads a new state.

#include "vkcore/controllers_impl.hpp"

#include "features/input/virtual_gamepad.hpp"
#include "vkcore/log.hpp"
#include "vkcore/mp_guard.hpp"

#include <Xinput.h>

#include <atomic>
#include <cstring>
#include <mutex>

namespace evr::vkcore::controllers {

namespace {

constexpr const char* kTag = "controllers";
constexpr WORD kGetStateOrdinal = 2;

using GetStateFn = DWORD(WINAPI*)(DWORD, XINPUT_STATE*);
std::atomic<GetStateFn> g_original{nullptr};

std::mutex g_padMutex;
input::PadState g_lastPad;
DWORD g_ourPackets = 0;
DWORD g_lastRealPacket = 0;
std::atomic<bool> g_loggedFirst{false};

input::PadState fromXInput(const XINPUT_GAMEPAD& g) {
    return {g.wButtons, g.bLeftTrigger, g.bRightTrigger, g.sThumbLX, g.sThumbLY, g.sThumbRX, g.sThumbRY};
}

XINPUT_GAMEPAD toXInput(const input::PadState& p) {
    XINPUT_GAMEPAD g{};
    g.wButtons = p.buttons;
    g.bLeftTrigger = p.leftTrigger;
    g.bRightTrigger = p.rightTrigger;
    g.sThumbLX = p.thumbLX;
    g.sThumbLY = p.thumbLY;
    g.sThumbRX = p.thumbRX;
    g.sThumbRY = p.thumbRY;
    return g;
}

DWORD WINAPI hookedGetState(DWORD user, XINPUT_STATE* out) {
    const GetStateFn original = g_original.load(std::memory_order_acquire);
    const DWORD result = original ? original(user, out) : static_cast<DWORD>(ERROR_DEVICE_NOT_CONNECTED);
    State& s = state();
    if (user != 0 || !out || !s.xinputActive.load(std::memory_order_acquire) ||
        !mp_guard::allowsGameTouch()) {
        return result;
    }
    if (!s.attached.load(std::memory_order_acquire)) {
        return result;
    }
    // Pad 0 stays connected while the controllers are attached, even while their input is stale (the
    // headset taken off), so the game never sees the pad come and go.
    const MappedInput mapped = runMapper();
    s.padReads.fetch_add(1, std::memory_order_relaxed);
    const bool connected = result == ERROR_SUCCESS;
    const input::PadState real = connected ? fromXInput(out->Gamepad) : input::PadState{};
    input::Axis2 look;
    if (game::contains(mapped.input.down, game::GameAction::WeaponWheel)) {
        look = mapped.input.wheelPointer;
    } else if (!s.angleHook) {
        look.x = mapped.turnStick.x; // no turn hook: turn through the game's own stick look
    }
    const input::PadState ours =
        mapped.live ? input::padStateFor(mapped.actions, mapped.input.move, look) : input::PadState{};
    const input::PadState merged = input::mergePads(real, ours);
    DWORD packets = 0;
    {
        // Our own counter, moved on whenever the merged state or the real pad's packet changes, so the
        // number never goes back when a real pad comes or goes.
        std::lock_guard lock(g_padMutex);
        const DWORD realPacket = connected ? out->dwPacketNumber : 0;
        if (!(merged == g_lastPad) || realPacket != g_lastRealPacket) {
            g_lastPad = merged;
            g_lastRealPacket = realPacket;
            ++g_ourPackets;
        }
        packets = g_ourPackets;
    }
    out->dwPacketNumber = packets;
    out->Gamepad = toXInput(merged);
    if (!g_loggedFirst.exchange(true)) {
        EVR_LOG("%s: the virtual gamepad answers the game's first read (a real pad is %s)", kTag,
                connected ? "connected" : "not connected");
    }
    return ERROR_SUCCESS;
}

bool isXInputModule(const char* name) {
    return _strnicmp(name, "xinput", 6) == 0;
}

} // namespace

bool installXInputHook() {
    auto* module = reinterpret_cast<std::byte*>(GetModuleHandleW(nullptr));
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(module);
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(module + dos->e_lfanew);
    const IMAGE_DATA_DIRECTORY& dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (!dir.VirtualAddress) {
        EVR_LOG("%s: the exe has no imports; no virtual gamepad", kTag);
        return false;
    }
    for (auto* desc = reinterpret_cast<const IMAGE_IMPORT_DESCRIPTOR*>(module + dir.VirtualAddress);
         desc->Name; ++desc) {
        const char* dll = reinterpret_cast<const char*>(module + desc->Name);
        if (!isXInputModule(dll) || !desc->OriginalFirstThunk) {
            continue;
        }
        auto* names = reinterpret_cast<const IMAGE_THUNK_DATA64*>(module + desc->OriginalFirstThunk);
        auto* slots = reinterpret_cast<IMAGE_THUNK_DATA64*>(module + desc->FirstThunk);
        for (; names->u1.AddressOfData; ++names, ++slots) {
            bool match = false;
            if (IMAGE_SNAP_BY_ORDINAL64(names->u1.Ordinal)) {
                match = IMAGE_ORDINAL64(names->u1.Ordinal) == kGetStateOrdinal;
            } else {
                const auto* byName =
                    reinterpret_cast<const IMAGE_IMPORT_BY_NAME*>(module + names->u1.AddressOfData);
                match = std::strcmp(reinterpret_cast<const char*>(byName->Name), "XInputGetState") == 0;
            }
            if (!match) {
                continue;
            }
            // The original is stored before the slot is switched, so a racing call never sees null.
            g_original.store(reinterpret_cast<GetStateFn>(slots->u1.Function), std::memory_order_release);
            DWORD old = 0;
            if (!VirtualProtect(&slots->u1.Function, sizeof(slots->u1.Function), PAGE_READWRITE, &old)) {
                EVR_LOG("%s: cannot unprotect the XInputGetState import; no virtual gamepad", kTag);
                return false;
            }
            slots->u1.Function = reinterpret_cast<ULONGLONG>(&hookedGetState);
            VirtualProtect(&slots->u1.Function, sizeof(slots->u1.Function), old, &old);
            EVR_LOG("%s: virtual gamepad: %s XInputGetState import replaced (IAT RVA 0x%X)", kTag, dll,
                    static_cast<unsigned>(reinterpret_cast<std::byte*>(&slots->u1.Function) - module));
            return true;
        }
    }
    EVR_LOG("%s: the exe does not import XInputGetState; no virtual gamepad", kTag);
    return false;
}

} // namespace evr::vkcore::controllers
