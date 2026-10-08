#include "vkcore/ssdo_menu_hook.hpp"

#include "stereo_seq/seq_settings.hpp"
#include "vkcore/game_text.hpp"
#include "vkcore/log.hpp"
#include "vkcore/mid_hook.hpp"
#include "vkcore/mp_guard.hpp"
#include "vkcore/taa_hooks.hpp"
#include "vkcore/taa_locate.hpp"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

namespace evr::vkcore {

namespace {

constexpr const char* kTag = "cvars";

// The Directional Occlusion setter (RVA 0x1420F20): `sub rsp, 0x28; mov [rcx + 0x1226C], edx`, the level's
// branches (0, 1, 2, 3 to 6), then `lea rcx, [r_SSDO]` at +0x27 for the setter of levels 3 to 6.
constexpr const char* kSetterSignature =
    "48 83 EC 28 89 91 6C 22 01 00 85 D2 74 ?? 83 FA 01 74 ?? 83 FA 02 74 ?? "
    "8D 42 FD 83 F8 03 0F 87 ?? ?? ?? ?? 41 B0 01 48 8D 0D";
constexpr std::size_t kSetterSsdoLea = 0x27;
constexpr int kLoggedCalls = 20;

std::once_flag g_installOnce;
std::atomic<int> g_choice{-1};
std::atomic<int> g_calls{0};

// At the setter's entry: edx is the level.
void onSetter(const HookRegisters& regs) {
    if (!mp_guard::allowsGameTouch()) {
        return;
    }
    const int level = static_cast<int>(static_cast<std::uint32_t>(regs.rdx));
    const auto c = stereo_seq::directionalOcclusionSsdoCvar(level);
    if (!c) {
        return;
    }
    const int value = c->value == "0" ? 0 : 1;
    const int before = g_choice.exchange(value);
    if (g_calls.fetch_add(1) < kLoggedCalls || before != value) {
        EVR_LOG("%s: the game's Directional Occlusion setting ran (level %d): r_SSDO %d", kTag, level, value);
    }
}

} // namespace

void installSsdoMenuHook() {
    std::call_once(g_installOnce, [] {
        if (!routeSRequested() || !mp_guard::allowsGameTouch()) {
            return;
        }
        GameImage image;
        if (!locateGameImage(image, kTag)) {
            return;
        }
        const std::byte* setter = findUnique(image, kTag, "Directional Occlusion setter", kSetterSignature);
        if (!setter) {
            EVR_LOG("%s: r_SSDO is held at its launch value, not at the game's Directional Occlusion setting "
                    "(the setter "
                    "was not found in this build)",
                    kTag);
            return;
        }
        const std::vector<std::byte*> cvars = findCvarObjects(image, {"r_SSDO"});
        const std::byte* lea = setter + kSetterSsdoLea;
        if (!cvars[0] || ripTarget(image, lea + 3, lea + 7) != cvars[0]) {
            EVR_LOG(
                "%s: the Directional Occlusion setter does not write r_SSDO; r_SSDO is held at its launch "
                "value",
                kTag);
            return;
        }
        std::string error;
        if (!installMidHook(const_cast<std::byte*>(setter), &onSetter, error)) {
            EVR_LOG("%s: Directional Occlusion setter hook failed: %s; r_SSDO is held at its launch value",
                    kTag, error.c_str());
            return;
        }
        EVR_LOG(
            "%s: Directional Occlusion setter hooked at RVA 0x%X: the r_SSDO hold follows the game's setting",
            kTag, image.rva(setter));
    });
}

int ssdoMenuChoice() {
    return g_choice.load();
}

} // namespace evr::vkcore
