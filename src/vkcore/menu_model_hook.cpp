#include "vkcore/menu_model_hook.hpp"

#include "vkcore/game_text.hpp"
#include "vkcore/log.hpp"
#include "vkcore/mid_hook.hpp"
#include "vkcore/mp_guard.hpp"
#include "vkcore/seh_filter.hpp"
#include "vkcore/stereo_hooks.hpp"
#include "vkcore/view_hook.hpp"

#include <windows.h>

#include <openxr/openxr.h>

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <memory>
#include <mutex>
#include <new>
#include <optional>
#include <string>

namespace evr::vkcore::menu_model {

namespace {

constexpr const char* kTag = "menu model";

// idMenuWidget_3D_Stand::UpdatePosition (RVA 0x15A6D30): `mov rax, [rax]; xor edx, edx; mov [rsp+0x158],
// rbx; call [rax+0x118]` (the world's RenderViewForIndex(0)), then `mov rbx, rax; test rax, rax; je <out>`.
// Unique in build 25216728 at RVA 0x15A6D84.
constexpr const char* kSignature =
    "48 8B 00 33 D2 48 89 9C 24 58 01 00 00 FF 90 18 01 00 00 48 8B D8 48 85 C0 0F 84";
constexpr std::size_t kViewHook = 0x13; // mov rbx, rax
// The reads of the view the function makes through rbx, besides the axis and origin below (offsets from the
// signature): `movss xmm0, [rbx+0x2C]` (fov_y), `mov rdx, r15`, `movss xmm3, [rbx+0x28]` (fov_x).
constexpr std::size_t kFovRead = 0xE4;
constexpr unsigned char kFovBytes[] = {0xF3, 0x0F, 0x10, 0x43, 0x2C, 0x49, 0x8B,
                                       0xD7, 0xF3, 0x0F, 0x10, 0x5B, 0x28};
// The scale goes out through rsi (the fourth argument): `movsd [rsi], xmm0` (scale x, y) ... `mov [rsi+8],
// eax`.
constexpr std::size_t kScaleWrite = 0x298;
constexpr unsigned char kScaleBytes[] = {0xF2, 0x0F, 0x11, 0x06};
// The position: `addss xmm5, [rbx+0x94]; addss xmm2, [rbx+0x9C]` (the origin), `movss [r12], xmm5;
// movss [r12+4], xmm3; movss [r12+8], xmm2` (r12, the second argument), then `movss xmm7, [rbx+0xAC]`, the
// first read of the axis for the model's rotation, where the second hook goes.
constexpr std::size_t kPositionWrite = 0x38C;
constexpr unsigned char kPositionBytes[] = {0xF3, 0x0F, 0x58, 0xAB, 0x94, 0x00, 0x00, 0x00, 0xF3, 0x0F, 0x58,
                                            0x93, 0x9C, 0x00, 0x00, 0x00, 0xF3, 0x41, 0x0F, 0x11, 0x2C, 0x24,
                                            0xF3, 0x41, 0x0F, 0x11, 0x5C, 0x24, 0x04, 0xF3, 0x41, 0x0F, 0x11,
                                            0x54, 0x24, 0x08, 0xF3, 0x0F, 0x10, 0xBB, 0xAC, 0x00, 0x00, 0x00};
constexpr std::size_t kPlacedHook = 0x3B0;

// The weapon's light rig. The weapon mod and customize screens show an entity (the widget's handle at
// +0x190); the widget's tick (RVA 0x15A8130) calls UpdatePosition with its position at [rsp+0x20], sets the
// entity's origin, axis and scale from it, then calls the entity's UpdateLightRig (vtbl+0x3F0, RVA 0xBE4930),
// which puts each rig light at origin + offset * axis with the offset and the light's size unscaled. Before
// that call: `mov rcx, [rbx+8]; mov rax, [rcx]; call [rax+0x3F0]` (rbx = the handle), then `mov rcx,
// [rsp+0x68]; xor rcx, rsp`. Unique in build 25216728 at RVA 0x15A83FD.
constexpr const char* kRigSignature = "48 8B 4B 08 48 8B 01 FF 90 F0 03 00 00 48 8B 4C 24 68 48 33 CC";
constexpr std::size_t kRigDoneHook = 0xD; // mov rcx, [rsp+0x68], also where the tick's early exits jump
constexpr std::uintptr_t kTickPositionFromRsp = 0x20;
// The entity's rig: entries at [+0x3F8], count [+0x400], 0x50 bytes each: a handle {id, cached id, idLight*}
// at +0 and the light's offset from the origin (3 floats) at +0x44.
constexpr std::size_t kRigEntries = 0x3F8;
constexpr std::size_t kRigCount = 0x400;
constexpr std::size_t kRigStride = 0x50;
constexpr std::size_t kRigOffset = 0x44;
// idLight fields and the render light's (idRenderLight = 0x18 bytes + renderLight_t) they are copied to at
// spawn (RVA 0xCAF6D0, 0xCB051D): type 0xB88 -> 0x74, intensity (colorScale) 0xBA8 -> 0x70, radius 0xBAC ->
// 0xA8 and centre 0xBB8 -> 0xB4 (3 floats each), maxVisibleRange 0xCDC -> 0x104, fadeVisibilityOver 0xCE0 ->
// 0x108, maxShadowVisibleRange 0xCE4 -> 0x10C; the render light at 0xD18.
constexpr std::size_t kLightType = 0xB88;
constexpr std::size_t kLightIntensity = 0xBA8;
constexpr std::size_t kLightRadius = 0xBAC;
constexpr std::size_t kLightCenter = 0xBB8;
constexpr std::size_t kLightRanges = 0xCDC;      // 3 floats: visible, fade over, shadow
constexpr std::size_t kLightCastShadows = 0xBCC; // bit 0, copied to the render light's castShadows
constexpr std::size_t kLightRender = 0xD18;
constexpr std::size_t kRenderIntensity = 0x70;
constexpr std::size_t kRenderRadius = 0xA8;
constexpr std::size_t kRenderCenter = 0xB4;
constexpr std::size_t kRenderRanges = 0x104;
// The render light's first flag byte: stationary 0x01, castShadows 0x02, ... (renderLight_t's bit fields).
constexpr std::size_t kRenderFlags = 0x88;
constexpr unsigned char kCastShadows = 0x02;
constexpr int kMaxRigLights = 16;

// The idRenderView RenderViewForIndex returns: operator new(0x29950) in the world constructor (RVA
// 0x18E29F8).
constexpr std::size_t kViewSize = render_view_object::kSize;
static_assert(kViewSize == 0x29950);

constexpr double kFreshSeconds = 0.25;

enum class Mode { Off, Panel, Near };

struct Shared {
    std::mutex mutex;
    std::optional<menu::PanelImage> panel; // the latest frame's, nullopt when the panel is not showing
    double panelSeconds = -1.0;
    std::optional<menu::WorldHead> head;
    float gameFovX = 0.0f;
    double headSeconds = -1.0;
};
Shared g_shared;

Mode g_mode = Mode::Panel; // set once at install
std::atomic<bool> g_loggedFirst{false};

// Per game thread: the copy of the render view handed to UpdatePosition (alive for the thread's life, so
// never a dangling pointer), and the camera of the call in progress.
struct ThreadState {
    std::unique_ptr<std::byte[]> view;
    bool pending = false;
    menu::ModelCamera camera;
    // The last model put on the panel: its position (the tick's [rsp+0x20]) and factor, for the light rig.
    std::uintptr_t placedAt = 0;
    float placedFactor = 1.0f;
    // The rig offsets grown for this thread's UpdateLightRig call, put back after it.
    struct SavedOffset {
        std::byte* at;
        float offset[3];
    };
    SavedOffset saved[kMaxRigLights] = {};
    int savedCount = 0;
};
thread_local ThreadState t_state;

// The entity whose light rig was last grown (its tick may run on any game thread), and whether the first
// grown rig was logged.
std::atomic<std::uintptr_t> g_grownEntity{0};
std::atomic<bool> g_loggedRig{false};

double nowSeconds() {
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

bool guardedCopy(void* destination, const void* source, std::size_t size) {
    __try {
        std::memcpy(destination, source, size);
        return true;
    } __except (accessViolationOnly(GetExceptionCode())) {
        return false;
    }
}

Mode modeFromEnv() {
    std::wstring value;
    if (!readEnv(L"ETERNALVR_MENU_MODEL_PANEL", value) || value.empty()) {
        return Mode::Panel;
    }
    if (value == L"0" || _wcsicmp(value.c_str(), L"off") == 0 || _wcsicmp(value.c_str(), L"false") == 0) {
        return Mode::Off;
    }
    return _wcsicmp(value.c_str(), L"near") == 0 ? Mode::Near : Mode::Panel;
}

std::optional<menu::ModelCamera> currentCamera() {
    std::lock_guard lock(g_shared.mutex);
    const double now = nowSeconds();
    if (!g_shared.panel || !g_shared.head || now - g_shared.panelSeconds > kFreshSeconds ||
        now - g_shared.headSeconds > kFreshSeconds) {
        return std::nullopt;
    }
    return menu::panelCamera(*g_shared.panel, *g_shared.head, g_shared.gameFovX);
}

void writeCamera(std::byte* view, const menu::ModelCamera& c) {
    const float fov[2] = {c.fovX, c.fovY};
    const float origin[3] = {c.origin.x, c.origin.y, c.origin.z};
    const float axis[9] = {c.forward.x, c.forward.y, c.forward.z, c.left.x, c.left.y,
                           c.left.z,    c.up.x,      c.up.y,      c.up.z};
    std::memcpy(view + render_view::kFovX, fov, sizeof(fov));
    std::memcpy(view + render_view::kViewOrigin, origin, sizeof(origin));
    std::memcpy(view + render_view::kViewAxis, axis, sizeof(axis));
}

// After RenderViewForIndex(0): the view the model is placed from becomes the panel camera.
void onRenderView(HookRegisters& regs) {
    ThreadState& t = t_state;
    t.pending = false;
    t.placedAt = 0;
    if (regs.rax == 0 || !mp_guard::allowsGameTouch()) {
        return;
    }
    const std::optional<menu::ModelCamera> camera = currentCamera();
    if (!camera) {
        return;
    }
    if (!t.view) {
        t.view.reset(new (std::nothrow) std::byte[kViewSize]);
        if (!t.view) {
            return;
        }
    }
    if (!guardedCopy(t.view.get(), reinterpret_cast<const void*>(regs.rax), kViewSize)) {
        return;
    }
    writeCamera(t.view.get(), *camera);
    if (!mp_guard::allowsGameTouch()) {
        return;
    }
    t.camera = *camera;
    t.pending = true;
    regs.rax = reinterpret_cast<std::uintptr_t>(t.view.get());
}

void logFirst(const menu::ModelCamera& c, const float* placed, float factor) {
    if (g_loggedFirst.exchange(true)) {
        return;
    }
    EVR_LOG("%s: the first model placed from the panel camera: camera (%.3f %.3f %.3f) fwd (%.3f %.3f %.3f), "
            "fov %.2f x %.2f, the panel %.3f unit(s) ahead; the model at (%.3f %.3f %.3f)%s%.2f",
            kTag, c.origin.x, c.origin.y, c.origin.z, c.forward.x, c.forward.y, c.forward.z, c.fovX, c.fovY,
            c.panelDistance, placed[0], placed[1], placed[2],
            g_mode == Mode::Near ? ", left near the camera (ETERNALVR_MENU_MODEL_PANEL=near), factor "
                                 : ", magnified onto the panel by ",
            factor);
}

// After the position is written: the model goes onto the panel's plane (its scale with it).
void onModelPlaced(HookRegisters& regs) {
    ThreadState& t = t_state;
    if (!t.pending) {
        return;
    }
    t.pending = false;
    if (regs.rbx != reinterpret_cast<std::uintptr_t>(t.view.get())) {
        return;
    }
    auto* positionAt = reinterpret_cast<std::byte*>(regs.r12);
    auto* scaleAt = reinterpret_cast<std::byte*>(regs.rsi);
    float position[3] = {};
    float scale[3] = {};
    if (!guardedCopy(position, positionAt, sizeof(position)) || !guardedCopy(scale, scaleAt, sizeof(scale))) {
        return;
    }
    if (g_mode == Mode::Near) {
        logFirst(t.camera, position, 1.0f);
        return;
    }
    const std::optional<menu::ModelOnPanel> on =
        menu::modelOnPanel(t.camera, {position[0], position[1], position[2]}, {scale[0], scale[1], scale[2]});
    if (!on || !mp_guard::allowsGameTouch()) {
        return;
    }
    const float placed[3] = {on->position.x, on->position.y, on->position.z};
    const float scaled[3] = {on->scale.x, on->scale.y, on->scale.z};
    if (guardedCopy(positionAt, placed, sizeof(placed)) && guardedCopy(scaleAt, scaled, sizeof(scaled))) {
        logFirst(t.camera, placed, on->factor);
        t.placedAt = regs.r12;
        t.placedFactor = on->factor;
    }
}

// The light of a rig entry, when its handle is resolved (id == cached id), as the game reads it.
std::byte* rigLight(const std::byte* entry) {
    std::int32_t ids[2] = {};
    std::byte* light = nullptr;
    if (!guardedCopy(ids, entry, sizeof(ids)) || ids[0] != ids[1] ||
        !guardedCopy(&light, entry + 8, sizeof(light))) {
        return nullptr;
    }
    return light;
}

// The offsets grown for this thread's UpdateLightRig call go back.
void restoreOffsets(ThreadState& t) {
    for (int i = 0; i < t.savedCount; ++i) {
        guardedCopy(t.saved[i].at, t.saved[i].offset, sizeof(t.saved[i].offset));
    }
    t.savedCount = 0;
}

// Before the entity's UpdateLightRig: when this tick's UpdatePosition put the model on the panel, the rig
// grows with it (menu::rigFactor): each point light's offset (for this call), radius, centre and visible
// ranges by the rig's factor, its intensity by that squared (menu::rigIntensityTrim), and its shadows go off
// (grown, the lights shadow the model onto itself, which on the flat screen they never do). A rig grown
// before and not magnified now gets its lights back as the engine has them, from each idLight.
void onLightRig(const HookRegisters& regs) {
    ThreadState& t = t_state;
    // A call that never reached the hook after it (an error unwound it) gives its offsets back first, so they
    // never grow twice.
    restoreOffsets(t);
    const bool placed = t.placedAt != 0 && t.placedAt == regs.rsp + kTickPositionFromRsp;
    const float modelFactor = placed ? t.placedFactor : 1.0f;
    t.placedAt = 0;
    std::uintptr_t entity = 0;
    if (!guardedCopy(&entity, reinterpret_cast<const void*>(regs.rbx + 8), sizeof(entity)) || entity == 0) {
        return;
    }
    if (!placed && entity != g_grownEntity.load()) {
        return;
    }
    if (!mp_guard::allowsGameTouch()) {
        return;
    }
    const auto* base = reinterpret_cast<const std::byte*>(entity);
    std::byte* entries = nullptr;
    std::int32_t rigCount = 0;
    if (!guardedCopy(&entries, base + kRigEntries, sizeof(entries)) ||
        !guardedCopy(&rigCount, base + kRigCount, sizeof(rigCount)) || entries == nullptr || rigCount <= 0) {
        g_grownEntity.store(0);
        return;
    }
    const std::int32_t count = rigCount > kMaxRigLights ? kMaxRigLights : rigCount;
    float offsets[kMaxRigLights][3] = {};
    float farthest = 0.0f;
    for (std::int32_t i = 0; i < count; ++i) {
        float* o = offsets[i];
        if (guardedCopy(o, entries + static_cast<std::size_t>(i) * kRigStride + kRigOffset,
                        sizeof(offsets[i]))) {
            const float length = std::sqrt(o[0] * o[0] + o[1] * o[1] + o[2] * o[2]);
            farthest = std::isfinite(length) && length > farthest ? length : farthest;
        }
    }
    const float factor = menu::rigFactor(modelFactor, farthest);
    const bool grow = factor != 1.0f;
    const float trim = grow ? menu::rigIntensityTrim(modelFactor, factor) : 1.0f;
    const bool describe = grow && !g_loggedRig.load();
    char lights[512] = "";
    std::size_t used = 0;
    for (std::int32_t i = 0; i < count; ++i) {
        std::byte* entry = entries + static_cast<std::size_t>(i) * kRigStride;
        // The light first: an entry whose light is not resolved yet (the game resolves it in the call) or not
        // a point light is left whole as it is, offset included.
        std::byte* light = rigLight(entry);
        std::int32_t type = 0;
        float radius[3] = {};
        float center[3] = {};
        float ranges[3] = {};
        unsigned char shadowSource = 0;
        menu::RigLight rig;
        std::byte* render = nullptr;
        if (light == nullptr || !guardedCopy(&type, light + kLightType, sizeof(type)) ||
            !guardedCopy(&rig.intensity, light + kLightIntensity, sizeof(rig.intensity)) ||
            !guardedCopy(radius, light + kLightRadius, sizeof(radius)) ||
            !guardedCopy(center, light + kLightCenter, sizeof(center)) ||
            !guardedCopy(ranges, light + kLightRanges, sizeof(ranges)) ||
            !guardedCopy(&shadowSource, light + kLightCastShadows, sizeof(shadowSource)) ||
            !guardedCopy(&render, light + kLightRender, sizeof(render)) || render == nullptr) {
            continue;
        }
        rig.type = type;
        rig.radius = {radius[0], radius[1], radius[2]};
        rig.center = {center[0], center[1], center[2]};
        rig.visibleRange = ranges[0];
        rig.fadeOver = ranges[1];
        rig.shadowRange = ranges[2];
        const std::optional<menu::RigLight> grown = menu::grownRigLight(rig, factor, trim);
        if (describe && used < sizeof(lights) - 1) {
            const int n =
                std::snprintf(lights + used, sizeof(lights) - used,
                              "; type %d offset (%.3f %.3f %.3f) radius (%.2f %.2f %.2f) intensity %.2f%s",
                              type, offsets[i][0], offsets[i][1], offsets[i][2], radius[0], radius[1],
                              radius[2], rig.intensity, grown ? "" : " (left as it is)");
            used = n > 0 ? used + static_cast<std::size_t>(n) : used;
        }
        if (!grown) {
            continue;
        }
        if (grow) {
            const float* o = offsets[i];
            const float scaled[3] = {o[0] * factor, o[1] * factor, o[2] * factor};
            ThreadState::SavedOffset& s = t.saved[t.savedCount];
            s.at = entry + kRigOffset;
            std::memcpy(s.offset, o, sizeof(s.offset));
            if (guardedCopy(entry + kRigOffset, scaled, sizeof(scaled))) {
                ++t.savedCount;
            }
        }
        const float r[3] = {grown->radius.x, grown->radius.y, grown->radius.z};
        const float c[3] = {grown->center.x, grown->center.y, grown->center.z};
        const float g[3] = {grown->visibleRange, grown->fadeOver, grown->shadowRange};
        guardedCopy(render + kRenderIntensity, &grown->intensity, sizeof(grown->intensity));
        guardedCopy(render + kRenderRadius, r, sizeof(r));
        guardedCopy(render + kRenderCenter, c, sizeof(c));
        guardedCopy(render + kRenderRanges, g, sizeof(g));
        // castShadows: off while grown, else the idLight's own, as the engine copies it (RVA 0xCAFD10).
        unsigned char flags = 0;
        if (guardedCopy(&flags, render + kRenderFlags, sizeof(flags))) {
            const unsigned char cast = grow ? 0 : static_cast<unsigned char>((shadowSource & 1) << 1);
            flags = static_cast<unsigned char>((flags & ~kCastShadows) | cast);
            guardedCopy(render + kRenderFlags, &flags, sizeof(flags));
        }
    }
    g_grownEntity.store(grow ? entity : 0);
    if (describe && !g_loggedRig.exchange(true)) {
        EVR_LOG(
            "%s: the first light rig grown with its model: model x%.2f, rig x%.2f (lights kept within %.1f "
            "units), intensity x%.2f, shadows off; entity %p, %d light(s)%s%s",
            kTag, modelFactor, factor, menu::kRigLightReach, factor * factor * trim,
            reinterpret_cast<void*>(entity), rigCount, rigCount > count ? " (only the first 16 grown)" : "",
            lights);
    }
}

// After UpdateLightRig (and where the tick's early exits land, with nothing saved): the offsets go back.
void onLightRigDone(const HookRegisters&) {
    restoreOffsets(t_state);
}

// The light rig hooks, after the placement hooks: without them the model is still on the panel, only lit as
// before.
void installRigHooks(const GameImage& image) {
    const std::byte* site = findUnique(image, kTag, "the widget tick's UpdateLightRig call", kRigSignature);
    if (!site) {
        EVR_LOG("%s: the weapon's light rig is not grown with the model", kTag);
        return;
    }
    std::string error;
    std::byte* before = const_cast<std::byte*>(site);
    std::byte* after = const_cast<std::byte*>(site + kRigDoneHook);
    if (!installMidHook(after, &onLightRigDone, error)) {
        EVR_LOG("%s: hook at RVA 0x%X failed: %s; the weapon's light rig is not grown with the model", kTag,
                image.rva(after), error.c_str());
        return;
    }
    if (!installMidHook(before, &onLightRig, error)) {
        EVR_LOG("%s: hook at RVA 0x%X failed: %s; the weapon's light rig is not grown with the model", kTag,
                image.rva(before), error.c_str());
        return;
    }
    EVR_LOG("%s: light rig hooks at RVA 0x%X and 0x%X: the weapon's lights grow with the model on the panel",
            kTag, image.rva(before), image.rva(after));
}

bool installOnce() {
    g_mode = modeFromEnv();
    if (g_mode == Mode::Off) {
        EVR_LOG("%s: off (ETERNALVR_MENU_MODEL_PANEL=0); a menu's 3D model stays in front of the head", kTag);
        return false;
    }
    GameImage image;
    if (!locateGameImage(image, kTag)) {
        return false;
    }
    const std::byte* site = findUnique(image, kTag, "idMenuWidget_3D_Stand::UpdatePosition view", kSignature);
    if (!site) {
        return false;
    }
    const auto matches = [&](std::size_t offset, const unsigned char* bytes, std::size_t size) {
        return image.inText(site + offset, size) && std::memcmp(site + offset, bytes, size) == 0;
    };
    if (!matches(kFovRead, kFovBytes, sizeof(kFovBytes)) ||
        !matches(kScaleWrite, kScaleBytes, sizeof(kScaleBytes)) ||
        !matches(kPositionWrite, kPositionBytes, sizeof(kPositionBytes))) {
        EVR_LOG("%s: UpdatePosition at RVA 0x%X does not read the view and write the model as expected; off",
                kTag, image.rva(site));
        return false;
    }
    std::byte* viewAt = const_cast<std::byte*>(site + kViewHook);
    std::byte* placedAt = const_cast<std::byte*>(site + kPlacedHook);
    std::string error;
    if (!installMidHookEdit(viewAt, &onRenderView, error)) {
        EVR_LOG("%s: hook at RVA 0x%X failed: %s; off", kTag, image.rva(viewAt), error.c_str());
        return false;
    }
    if (!installMidHookEdit(placedAt, &onModelPlaced, error)) {
        // The first hook stays, harmless alone: it only moves the camera the model is placed from.
        EVR_LOG("%s: hook at RVA 0x%X failed: %s; the model is placed from the panel camera but not moved "
                "onto the panel",
                kTag, image.rva(placedAt), error.c_str());
        g_mode = Mode::Near;
        return true;
    }
    EVR_LOG("%s: hooks at RVA 0x%X (the render view) and 0x%X (the placed model); while the menu panel shows "
            "over a head-tracked frame, a menu's 3D model is placed from a camera at the panel%s",
            kTag, image.rva(viewAt), image.rva(placedAt),
            g_mode == Mode::Near ? " and left near it (ETERNALVR_MENU_MODEL_PANEL=near)" : " and put on it");
    if (g_mode == Mode::Panel) {
        installRigHooks(image);
    }
    return true;
}

} // namespace

bool install() {
    static const bool installed = installOnce();
    return installed;
}

void notePanel(bool showing,
               const XrCompositionLayerQuad& quad,
               std::uint32_t imageWidth,
               std::uint32_t imageHeight) {
    std::optional<menu::PanelImage> image;
    if (showing) {
        image.emplace();
        const XrPosef& p = quad.pose;
        image->panel.pose = {{p.orientation.x, p.orientation.y, p.orientation.z, p.orientation.w},
                             {p.position.x, p.position.y, p.position.z}};
        image->panel.width = quad.size.width;
        image->panel.height = quad.size.height;
        image->imageWidth = imageWidth;
        image->imageHeight = imageHeight;
        const XrRect2Di& rect = quad.subImage.imageRect;
        image->rectX = static_cast<float>(rect.offset.x);
        image->rectY = static_cast<float>(rect.offset.y);
        image->rectWidth = static_cast<float>(rect.extent.width);
        image->rectHeight = static_cast<float>(rect.extent.height);
    }
    std::lock_guard lock(g_shared.mutex);
    g_shared.panel = image;
    g_shared.panelSeconds = nowSeconds();
}

void noteHead(const menu::WorldHead& head, float gameFovX) {
    std::lock_guard lock(g_shared.mutex);
    g_shared.head = head;
    g_shared.gameFovX = gameFovX;
    g_shared.headSeconds = nowSeconds();
}

} // namespace evr::vkcore::menu_model
