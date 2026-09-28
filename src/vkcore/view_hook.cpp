#include "vkcore/view_hook.hpp"

#include "vkcore/game_text.hpp"
#include "vkcore/log.hpp"
#include "vkcore/mid_hook.hpp"

#include <mutex>
#include <shared_mutex>
#include <string>

namespace evr::vkcore {

namespace {

// engine-facts.md section 5, "Build point: player view write" (RVA 0x6A311E in build 25216728): the
// normal path's vieworg and viewaxis stores relative to r14.
constexpr const char* kViewWriteSignature = "49 8B 07 49 8B CF FF 90 ?? ?? ?? ?? 49 8B CF F2 0F 10 00 F2 41 "
                                            "0F 11 86 ?? ?? ?? ?? 8B 40 08 41 89 86 ?? ?? ?? ?? "
                                            "49 8B 07 FF 90 ?? ?? ?? ?? 0F 10 00 41 0F 11 86 ?? ?? ?? ?? 0F "
                                            "10 48 10 41 0F 11 8E ?? ?? ?? ?? 8B 40 20";
constexpr std::size_t kViewWriteOriginDisp = 0x18;   // movsd [r14+d], xmm0  (vieworg.xy)
constexpr std::size_t kViewWriteOriginZDisp = 0x22;  // mov [r14+d], eax      (vieworg.z)
constexpr std::size_t kViewWriteAxisDisp = 0x36;     // movups [r14+d], xmm0 (viewaxis[0..3])
constexpr std::size_t kViewWriteAxisHighDisp = 0x42; // movups [r14+d], xmm1 (viewaxis[4..7])

// The join after the last viewaxis store (RVA 0x6A31A0), where both paths meet; the hook goes on the
// instruction after the join store (RVA 0x6A31B7), which every path reaches.
constexpr const char* kViewJoinSignature = "41 0F 11 86 ?? ?? ?? ?? 41 0F 11 8E ?? ?? ?? ?? 41 89 86 ?? ?? "
                                           "?? ?? 49 8B 07 49 8B CF FF 90 ?? ?? ?? ?? 84 C0 0F 84";
constexpr std::size_t kJoinAxisDisp = 4;
constexpr std::size_t kJoinAxisHighDisp = 12;
constexpr std::size_t kJoinAxisLastDisp = 19;
constexpr std::size_t kJoinPatch = 23;

// engine-facts.md section 5, "Render latch (r = g)" (RVA 0x1CE1441): `add rcx, offsetof(r)` then the
// renderView_t copy; rdi holds the idRenderView. The hook goes after the copy returns (RVA 0x1CE1464).
constexpr const char* kRenderLatchSignature = "49 89 5B 10 48 8B D1 4D 89 63 18 48 81 C1 ?? ?? ?? ?? 4D 89 "
                                              "73 20 45 0F 29 B3 ?? ?? ?? ?? E8 ?? ?? ?? ?? "
                                              "45 33 E4 44 38 A7 ?? ?? ?? ?? 75 2A";
constexpr std::size_t kLatchROffsetImm = 14;
constexpr std::size_t kLatchPatch = 35;
constexpr std::int32_t kExpectedROffset = 0x289D0; // idRenderView::r

std::shared_mutex g_sinkMutex;
ViewHookSink* g_sink = nullptr;

std::once_flag g_installOnce;
ViewHookStatus g_status;

// Set once before the hooks are installed, read-only afterwards.
std::intptr_t g_viewFromR14 = 0; // renderView_t = r14 + this
std::intptr_t g_rFromRdi = 0;    // idRenderView::r = rdi + this

constexpr const char* kTag = "hooks";

void onGameViewHook(const HookRegisters& regs) {
    std::shared_lock lock(g_sinkMutex);
    if (g_sink) {
        g_sink->onGameView(reinterpret_cast<std::byte*>(static_cast<std::intptr_t>(regs.r14) + g_viewFromR14),
                           reinterpret_cast<std::byte*>(regs.r15));
    }
}

void onRenderLatchHook(const HookRegisters& regs) {
    std::shared_lock lock(g_sinkMutex);
    if (g_sink) {
        const auto* r = reinterpret_cast<const std::byte*>(static_cast<std::intptr_t>(regs.rdi) + g_rFromRdi);
        g_sink->onRenderLatch(r, reinterpret_cast<const float*>(r + render_view::kSize));
    }
}

bool installGameViewHook(const GameImage& text) {
    const std::byte* write = findUnique(text, kTag, "view write", kViewWriteSignature);
    const std::byte* join = findUnique(text, kTag, "view join", kViewJoinSignature);
    if (!write || !join) {
        return false;
    }
    const std::int32_t origin = readI32(write + kViewWriteOriginDisp);
    const bool consistent =
        readI32(write + kViewWriteOriginZDisp) == origin + 8 &&
        readI32(write + kViewWriteAxisDisp) == origin + 0xC &&
        readI32(write + kViewWriteAxisHighDisp) == origin + 0x1C &&
        readI32(join + kJoinAxisDisp) == origin + 0xC && readI32(join + kJoinAxisHighDisp) == origin + 0x1C &&
        readI32(join + kJoinAxisLastDisp) == origin + 0x2C && join > write && join - write < 0x100;
    if (!consistent) {
        EVR_LOG("hooks: view write and join do not agree on the renderView_t layout; game view hook off");
        return false;
    }
    // r14 + origin is vieworg (renderView_t + 0x94).
    g_viewFromR14 = static_cast<std::intptr_t>(origin) - static_cast<std::intptr_t>(render_view::kViewOrigin);
    void* patch = const_cast<std::byte*>(join + kJoinPatch);
    std::string error;
    if (!installMidHook(patch, &onGameViewHook, error)) {
        EVR_LOG("hooks: game view hook at RVA 0x%X failed: %s", text.rva(join + kJoinPatch), error.c_str());
        return false;
    }
    EVR_LOG("hooks: game view hook at RVA 0x%X (renderView_t = r14 %c 0x%llX)", text.rva(join + kJoinPatch),
            g_viewFromR14 < 0 ? '-' : '+',
            static_cast<unsigned long long>(g_viewFromR14 < 0 ? -g_viewFromR14 : g_viewFromR14));
    return true;
}

bool installRenderLatchHook(const GameImage& text) {
    const std::byte* latch = findUnique(text, kTag, "render latch", kRenderLatchSignature);
    if (!latch) {
        return false;
    }
    const std::int32_t rOffset = readI32(latch + kLatchROffsetImm);
    if (rOffset != kExpectedROffset) {
        EVR_LOG("hooks: render latch copies to idRenderView+0x%X, expected 0x%X; hook off", rOffset,
                kExpectedROffset);
        return false;
    }
    g_rFromRdi = rOffset;
    void* patch = const_cast<std::byte*>(latch + kLatchPatch);
    std::string error;
    if (!installMidHook(patch, &onRenderLatchHook, error)) {
        EVR_LOG("hooks: render latch hook failed: %s", error.c_str());
        return false;
    }
    EVR_LOG("hooks: render latch hook at RVA 0x%X", text.rva(latch + kLatchPatch));
    return true;
}

} // namespace

ViewHookStatus installViewHooks(ViewHookSink* sink) {
    setViewHookSink(sink);
    std::call_once(g_installOnce, [] {
        GameImage text;
        if (!locateGameImage(text, kTag)) {
            EVR_LOG("hooks: the game module has no readable .text; hooks off");
            return;
        }
        g_status.gameView = installGameViewHook(text);
        // Only useful with the game view hook (it matches the views that hook wrote).
        g_status.renderLatch = g_status.gameView && installRenderLatchHook(text);
    });
    return g_status;
}

void setViewHookSink(ViewHookSink* sink) {
    std::unique_lock lock(g_sinkMutex);
    g_sink = sink;
}

} // namespace evr::vkcore
