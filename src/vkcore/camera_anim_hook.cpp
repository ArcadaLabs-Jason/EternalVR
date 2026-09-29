#include "vkcore/camera_anim_hook.hpp"

#include "vkcore/game_text.hpp"
#include "vkcore/log.hpp"
#include "vkcore/mid_hook.hpp"
#include "xr_math/camera_anim.hpp"

#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <cwchar>
#include <mutex>
#include <numbers>
#include <string>

namespace evr::vkcore::camera_anim {

namespace {

constexpr const char* kTag = "camera";

// idPlayer::CalculateViewWithoutUpdates (0x1451EE0), after the camera joint's transform became angles
// (RVA 0x1452564): `call ToAngles; mov rax, [p_debugAnimatedCamera]; movss xmm12..14, [rsp+0x58/0x5C/0x60]
// (offset); cmp [rax+8], 0; movss xmm10, [rsp+0x50]; movss xmm11, [rsp+0x4C]; movss xmm9, [rsp+0x48]
// (roll, yaw, pitch); je <no debug print>`. The je's target (RVA 0x14526C5) is where the debug print and
// the plain path meet, before the offset and angles are added: the hook goes there.
constexpr const char* kSignature =
    "E8 ?? ?? ?? ?? 48 8B 05 ?? ?? ?? ?? F3 44 0F 10 64 24 58 F3 44 0F 10 6C 24 5C "
    "F3 44 0F 10 74 24 60 83 78 08 00 F3 44 0F 10 54 24 50 F3 44 0F 10 5C 24 4C "
    "F3 44 0F 10 4C 24 48 0F 84 ?? ?? ?? ??";
constexpr std::size_t kJumpDisp = 0x3C; // the je's rel32
constexpr std::size_t kJumpEnd = 0x40;  // the instruction after the je
// At the join: `movss xmm5, [rsi+4]`, the first read of the view axis (rsi) the angles are added to.
constexpr unsigned char kJoinBytes[] = {0xF3, 0x0F, 0x10, 0x6E, 0x04};
constexpr std::size_t kAnglesFromRsp = 0x48;
// The largest angle a camera joint is believed to add; anything else is not these stack slots.
constexpr float kMaxDegrees = 360.0f;

struct Shared {
    std::mutex mutex;
    const std::byte* player = nullptr;
    xr_math::IdAngles added;
    std::uint64_t count = 0; // hook calls with plausible values
    std::uint64_t taken = 0; // `count` at the last take
};
Shared g_shared;

struct Episodes {
    bool playing = false;
    std::uint64_t started = 0;
    std::uint64_t frames = 0;
    float peak = 0.0f;
    bool applied = true;
    bool forced = false;
};
Episodes g_episodes; // camera hook thread only

// The gap between the rendered view and the player's view angles over one forced view.
struct ForcedGap {
    bool on = false;
    std::uint64_t episodes = 0;
    std::uint64_t frames = 0;
    float angle = 0.0f; // between the two forward vectors, degrees
    xr_math::IdAngles largest;
};
ForcedGap g_forced; // camera hook thread only

struct Sample {
    bool fresh = false;
    xr_math::IdAngles added;
};

bool g_apply = false;           // ETERNALVR_CAMERA_ANIMATIONS=1, set once at install
xr_math::CameraAnimRamp g_ramp; // ETERNALVR_CAMERA_ANIM_MIN=<degrees> (rig tests): start there, full at twice

bool plausible(float v) {
    return std::isfinite(v) && std::fabs(v) <= kMaxDegrees;
}

void onCameraJoint(const HookRegisters& regs) {
    float angles[3];
    std::memcpy(angles, reinterpret_cast<const void*>(regs.rsp + kAnglesFromRsp), sizeof(angles));
    if (!plausible(angles[0]) || !plausible(angles[1]) || !plausible(angles[2])) {
        return;
    }
    std::lock_guard lock(g_shared.mutex);
    g_shared.player = reinterpret_cast<const std::byte*>(regs.rdi);
    g_shared.added = {angles[0], angles[1], angles[2]};
    ++g_shared.count;
}

void noteFrame(float weight, float size, bool applied, bool forcedView) {
    Episodes& e = g_episodes;
    if (weight > 0.0f) {
        if (!e.playing) {
            e = Episodes{true, e.started + 1, 0, 0.0f, true, false};
        }
        ++e.frames;
        e.peak = std::fmax(e.peak, size);
        e.applied = e.applied && applied;
        e.forced = e.forced || forcedView;
        if (e.frames == 1 && e.started <= 50) {
            EVR_LOG("%s: camera animation %llu starts (%.1f deg; forced view %s); %s", kTag,
                    static_cast<unsigned long long>(e.started), size, forcedView ? "yes" : "no",
                    applied ? "its rotation goes on top of the head" : "the head replaces its rotation");
        }
        return;
    }
    if (e.playing) {
        e.playing = false;
        if (e.started <= 50) {
            EVR_LOG("%s: camera animation %llu ends after %llu game frame(s), largest angle %.1f deg; %s%s",
                    kTag, static_cast<unsigned long long>(e.started),
                    static_cast<unsigned long long>(e.frames), e.peak,
                    e.applied ? "played on top of the head" : "the head replaced its rotation",
                    e.forced ? " (a forced view took part)" : "");
        }
    }
}

} // namespace

bool install(const GameImage& image) {
    std::wstring value;
    g_apply = readEnv(L"ETERNALVR_CAMERA_ANIMATIONS", value) && value == L"1";
    if (readEnv(L"ETERNALVR_CAMERA_ANIM_MIN", value) && !value.empty()) {
        const float least = std::wcstof(value.c_str(), nullptr);
        if (std::isfinite(least) && least >= 0.5f && least <= 45.0f) {
            g_ramp = {least, 2.0f * least};
        }
    }
    const std::byte* site = findUnique(image, kTag, "animated camera", kSignature);
    if (!site) {
        return false;
    }
    const std::byte* join = site + kJumpEnd + readI32(site + kJumpDisp);
    if (!image.inText(join, sizeof(kJoinBytes)) || std::memcmp(join, kJoinBytes, sizeof(kJoinBytes)) != 0) {
        EVR_LOG("%s: the animated camera's join at RVA 0x%X is not the view axis read expected; hook off",
                kTag, image.rva(join));
        return false;
    }
    std::string error;
    if (!installMidHook(const_cast<std::byte*>(join), &onCameraJoint, error)) {
        EVR_LOG("%s: animated camera hook at RVA 0x%X failed: %s", kTag, image.rva(join), error.c_str());
        return false;
    }
    EVR_LOG(
        "%s: animated camera hook at RVA 0x%X (the hands animation's camera joint); animations of %.1f deg "
        "or more are logged, %s",
        kTag, image.rva(join), g_ramp.startDegrees,
        g_apply ? "their rotation goes on top of the head (ETERNALVR_CAMERA_ANIMATIONS=1)"
                : "the head replaces their rotation (ETERNALVR_CAMERA_ANIMATIONS=1 plays it)");
    return true;
}

Frame frame(const std::byte* player, const xr_math::IdViewAxis& gameAxis, bool cutscene, bool forcedView) {
    Frame f{gameAxis, {}, false};
    Sample sample;
    {
        std::lock_guard lock(g_shared.mutex);
        if (g_shared.count != g_shared.taken && g_shared.player == player && player) {
            sample = {true, g_shared.added};
        }
        g_shared.taken = g_shared.count;
    }
    const float weight = sample.fresh && !cutscene ? xr_math::cameraAnimWeight(sample.added, g_ramp) : 0.0f;
    if (weight > 0.0f && g_apply) {
        if (const auto base = xr_math::removeCameraAnim(gameAxis, sample.added)) {
            f = {*base, xr_math::scaleAngles(sample.added, weight), true};
        }
    }
    noteFrame(weight, xr_math::cameraAnimSize(sample.added), f.active, forcedView);
    return f;
}

void noteForcedView(bool forcedView,
                    const xr_math::IdViewAxis& gameAxis,
                    const xr_math::IdAngles& viewAngles) {
    ForcedGap& g = g_forced;
    if (forcedView) {
        if (!g.on) {
            g = ForcedGap{true, g.episodes + 1, 0, 0.0f, {}};
        }
        ++g.frames;
        const xr_math::IdViewAxis own = xr_math::axisFromAngles(viewAngles);
        const float cosine = std::clamp(dot(own.forward, gameAxis.forward), -1.0f, 1.0f);
        g.angle = std::fmax(g.angle, std::acos(cosine) * 180.0f / std::numbers::pi_v<float>);
        const xr_math::IdAngles game = xr_math::anglesFromAxis(gameAxis);
        const auto larger = [](float& keep, float gap) {
            if (std::fabs(gap) > std::fabs(keep)) {
                keep = gap;
            }
        };
        larger(g.largest.pitch, xr_math::normalize180(game.pitch - viewAngles.pitch));
        larger(g.largest.yaw, xr_math::normalize180(game.yaw - viewAngles.yaw));
        larger(g.largest.roll, xr_math::normalize180(game.roll - viewAngles.roll));
        return;
    }
    if (g.on) {
        g.on = false;
        if (g.episodes <= 50) {
            EVR_LOG("%s: forced view %llu: %llu game frame(s); the rendered view left the player's view "
                    "angles by up "
                    "to %.1f deg (largest pitch %.1f, yaw %.1f, roll %.1f)",
                    kTag, static_cast<unsigned long long>(g.episodes),
                    static_cast<unsigned long long>(g.frames), g.angle, g.largest.pitch, g.largest.yaw,
                    g.largest.roll);
        }
    }
}

} // namespace evr::vkcore::camera_anim
