// Motion controllers, look-at triggers (controllers.hpp).
//
// A few places open only when the player looks at something: the ladder panel, the tram exit and a door in
// Doom Hunter Base, and others through the campaign (idTrigger_Facing). The trigger's test
// (idTrigger_Facing's override of idTrigger slot 599, RVA 0xD9D0C0 in build 25216728) takes the look
// direction from the player's first-person view axis, which follows the weapon hand under hand aim, so in VR
// the trigger waited for the gun to point at the target. It copies the axis's forward row to [rsp+0x40..0x48]
// and then calls the player's view-origin getter; a mid hook on that call (rsi = the idPlayer) puts the
// head's horizontal forward in the copy. The test only uses the horizontal direction. Nothing else reads that
// copy. ETERNALVR_LOOK_TRIGGERS=0 leaves the gun's direction and only logs the tests.

#include "vkcore/controllers_impl.hpp"

#include "vkcore/game_text.hpp"
#include "vkcore/log.hpp"
#include "vkcore/mid_hook.hpp"
#include "vkcore/mp_guard.hpp"

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>

namespace evr::vkcore::controllers {

namespace {

constexpr const char* kTag = "controllers";

// mov rcx, rsi; call [rax+0x470] (the view axis); mov rcx, rsi; the forward row copied to [rsp+0x40..0x48];
// mov rax, [rsi]; call [rax+0x478] (the view origin); mov rcx, rdi. Unique in the Steam and Game Pass builds.
constexpr const char* kFacingSignature =
    "48 8B CE FF 90 70 04 00 00 48 8B CE F2 0F 10 00 8B 40 08 89 44 24 48 "
    "48 8B 06 F2 0F 11 44 24 40 FF 90 78 04 00 00 48 8B CF";
constexpr std::size_t kFacingHook = 32; // the origin getter's call
constexpr std::uintptr_t kForwardFromRsp = 0x40;
constexpr double kFreshSeconds = 0.25;

std::atomic<float> g_viewX{0.0f};
std::atomic<float> g_viewY{0.0f};
std::atomic<double> g_viewSeconds{-1.0};
std::atomic<std::uint64_t> g_tests{0};
// The game runs the test every frame the player stands in a trigger, until it passes once: the first three
// are logged, then one a minute.
LogCap g_testLines{3};

bool enabled() {
    static const bool on = [] {
        std::wstring value;
        return !(readEnv(L"ETERNALVR_LOOK_TRIGGERS", value) && value == L"0");
    }();
    return on;
}

double nowSeconds() {
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

void onFacing(const HookRegisters& regs) {
    if (!mp_guard::allowsGameTouch() || nowSeconds() - g_viewSeconds.load() > kFreshSeconds) {
        return;
    }
    if (!state().player.isPlayer(reinterpret_cast<const std::byte*>(regs.rsi))) {
        return;
    }
    auto* forwardAt = reinterpret_cast<std::byte*>(regs.rsp + kForwardFromRsp);
    float forward[3] = {};
    if (!safeCopy(forward, forwardAt, sizeof(forward))) {
        return;
    }
    const float head[2] = {g_viewX.load(), g_viewY.load()};
    const bool replace = enabled();
    if (replace && mp_guard::allowsGameTouch()) {
        safeCopy(forwardAt, head, sizeof(head)); // z stays: the test is horizontal
    }
    const std::uint64_t n = g_tests.fetch_add(1) + 1;
    std::uint64_t skipped = 0;
    if (g_testLines.due(GetTickCount64(), skipped)) {
        EVR_LOG("%s: look-at trigger test %llu: head yaw %.1f, view yaw %.1f%s", kTag,
                static_cast<unsigned long long>(n), std::atan2(head[1], head[0]) * 57.29578f,
                std::atan2(forward[1], forward[0]) * 57.29578f,
                replace ? "; the head's is tested" : " (ETERNALVR_LOOK_TRIGGERS=0: the view's is tested)");
    }
}

} // namespace

void noteViewForward(float x, float y) {
    const float length = std::sqrt(x * x + y * y);
    if (!(length > 1e-3f) || !std::isfinite(length)) {
        return; // straight up or down: no horizontal direction
    }
    g_viewX.store(x / length);
    g_viewY.store(y / length);
    g_viewSeconds.store(nowSeconds());
}

bool installFacingHook() {
    GameImage image;
    if (!locateGameImage(image, kTag)) {
        return false;
    }
    const std::byte* site = findUnique(image, kTag, "look-at trigger", kFacingSignature);
    if (!site) {
        return false;
    }
    std::string error;
    if (!installMidHook(const_cast<std::byte*>(site + kFacingHook), &onFacing, error)) {
        EVR_LOG("%s: look-at trigger hook at RVA 0x%X failed: %s", kTag, image.rva(site + kFacingHook),
                error.c_str());
        return false;
    }
    EVR_LOG("%s: look-at trigger hook at RVA 0x%X", kTag, image.rva(site + kFacingHook));
    return true;
}

} // namespace evr::vkcore::controllers
