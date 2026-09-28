// Motion controllers, the game's rumble for the vibration (controllers.hpp, haptics_xr.cpp).
//
// The game mixes the local player's rumble every frame whether or not a pad is connected
// (idRumbleComponent::Update); the pad path drops it later, with keyboard and mouse input, in the engine's
// joystick code. idRumbleComponent::GetMagnitudes (RVA 0xAAE730 in build 25216728) turns the mix into the
// frame's motor values: GetMagnitudes(this, int* high, int* low, int* left, int* right), high and low
// 0..65535 (the screen shake added, not clamped), the trigger ones 0..255. Its only caller is the
// render-view build, for the locally controlled player, before the game's own Vibration option zeroes the
// values. The detour calls the game's function and reads the two motors it wrote.

#include "vkcore/controllers_impl.hpp"

#include "vkcore/game_text.hpp"
#include "vkcore/log.hpp"
#include "vkcore/mid_hook.hpp"
#include "vkcore/mp_guard.hpp"

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <string>

namespace evr::vkcore::controllers {

namespace {

constexpr const char* kTag = "haptics";

// The whole prologue (no RIP-relative operand in the 21 bytes the detour moves), then the read of
// totalHighMag (+0x190) and mov rbx, r9.
constexpr const char* kMagnitudesSignature =
    "48 89 5C 24 08 48 89 74 24 10 48 89 7C 24 18 41 56 48 83 EC 30 F3 0F 10 81 90 01 00 00 49 8B D9";
constexpr float kMotorScale = 65535.0f;

using MagnitudesFn = void (*)(void* component, int* high, int* low, int* left, int* right);
MagnitudesFn g_original = nullptr;
std::atomic<bool> g_loggedFirst{false};

float motor(int value) {
    return static_cast<float>(std::clamp(value, 0, 65535)) / kMotorScale;
}

void onMagnitudes(void* component, int* high, int* low, int* left, int* right) {
    g_original(component, high, low, left, right);
    if (!mp_guard::allowsGameTouch()) {
        return;
    }
    int h = 0;
    int l = 0;
    // The game's own frame record; read safely all the same.
    if (!safeRead(reinterpret_cast<const std::byte*>(high), h) ||
        !safeRead(reinterpret_cast<const std::byte*>(low), l)) {
        return;
    }
    noteGameRumble(motor(l), motor(h));
    if ((h > 0 || l > 0) && !g_loggedFirst.exchange(true)) {
        EVR_LOG("%s: the game's first rumble: low %d, high %d", kTag, l, h);
    }
}

} // namespace

bool installRumbleHook() {
    GameImage image;
    if (!locateGameImage(image, kTag)) {
        return false;
    }
    const std::byte* site = findUnique(image, kTag, "rumble magnitudes", kMagnitudesSignature);
    if (!site) {
        return false;
    }
    std::string error;
    if (!installInlineHook(const_cast<std::byte*>(site), reinterpret_cast<void*>(&onMagnitudes),
                           reinterpret_cast<void**>(&g_original), error)) {
        EVR_LOG("%s: rumble hook at RVA 0x%X failed: %s", kTag, image.rva(site), error.c_str());
        return false;
    }
    EVR_LOG("%s: rumble hook at RVA 0x%X", kTag, image.rva(site));
    return true;
}

} // namespace evr::vkcore::controllers
