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
#include <cstddef>
#include <cstdint>
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
};
thread_local ThreadState t_state;

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
    }
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
