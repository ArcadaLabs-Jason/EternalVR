#pragma once

// Engine camera hooks (docs/rig-findings/engine-facts.md sections 2, 4.2, 4.3 and 5).
//
// Two mid-function hooks in DOOMEternalx64vk.exe, each located by signature in the loaded module and
// checked for a unique match plus the structure offsets its instructions encode. Anything
// unexpected leaves the game untouched and logs why.
//
// - Game view: the render-view build point, just after both of its paths have stored player 0's final
//   vieworg and viewaxis into gameFrameReturn_t. The callback may rewrite that renderView_t (origin,
//   axis, FOV) before the engine hands it to the renderer. Runs on the game-frame thread.
// - Render latch: in idRenderWorldLocal::Render, just after `r = g` copies the game's view for the
//   renderer. Read-only: tells which game frame the render thread is drawing, and exposes the
//   previous frame's projection matrix for checks.

#include <cstddef>
#include <cstdint>

namespace evr::vkcore {

// renderView_t field offsets (build 25216728; stable fields of the type-info record).
namespace render_view {
inline constexpr std::size_t kInCutscene = 0x15;
inline constexpr std::size_t kCameraCut = 0x16;
inline constexpr std::size_t kFovX = 0x28;
inline constexpr std::size_t kFovY = 0x2C;
inline constexpr std::size_t kZNear = 0x48;
inline constexpr std::size_t kViewOrigin = 0x94;
inline constexpr std::size_t kViewAxis = 0xA0;
// usesViewOriginOffset (a byte), localViewOrigin: a cutscene camera renders relative to viewOriginOffset
// (+0xD4), and meshes are then drawn from viewOriginOffset + localViewOrigin, not from vieworg.
inline constexpr std::size_t kUsesViewOriginOffset = 0xC4;
inline constexpr std::size_t kLocalViewOrigin = 0xC8;
inline constexpr std::size_t kSize = 0x970;

// Moves the view's origin by (x, y, z): vieworg, and localViewOrigin too while the view renders relative to
// viewOriginOffset (a cutscene camera), where a moved vieworg alone changes nothing on screen.
inline void moveViewOrigin(std::byte* renderView, float x, float y, float z) {
    auto* origin = reinterpret_cast<float*>(renderView + kViewOrigin);
    origin[0] += x;
    origin[1] += y;
    origin[2] += z;
    if (renderView[kUsesViewOriginOffset] != std::byte{0}) {
        auto* local = reinterpret_cast<float*>(renderView + kLocalViewOrigin);
        local[0] += x;
        local[1] += y;
        local[2] += z;
    }
}
} // namespace render_view

class ViewHookSink {
public:
    virtual ~ViewHookSink() = default;
    // Player 0's renderView_t for this game frame; the sink may modify it. player is the object the
    // build point asked for the view (r15 there, the local idPlayer in first-person play); unverified.
    virtual void onGameView(std::byte* renderView, std::byte* player) = 0;
    // The renderer's copy of a view (idRenderView::r) just after the latch, and the projection matrix
    // (16 floats) left from the previous render of this idRenderView.
    virtual void onRenderLatch(const std::byte* renderView, const float* previousProjection) = 0;
};

struct ViewHookStatus {
    bool gameView = false;
    bool renderLatch = false;
};

// Locates and installs both hooks once per process (later calls return the first result). The sink
// receives callbacks from then on; pass it again with setViewHookSink after a presenter restarts.
ViewHookStatus installViewHooks(ViewHookSink* sink);

// Replaces the sink. Waits for callbacks in progress, so after setViewHookSink(nullptr) returns the
// old sink is never called again.
void setViewHookSink(ViewHookSink* sink);

} // namespace evr::vkcore
