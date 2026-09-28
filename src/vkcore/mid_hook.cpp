#include "vkcore/mid_hook.hpp"

#include <safetyhook.hpp>

#include <windows.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <mutex>
#include <utility>

namespace evr::vkcore {

namespace {

std::array<std::atomic<MidHookCallback>, kMaxMidHooks> g_callbacks{};
std::array<std::atomic<MidHookEditCallback>, kMaxMidHooks> g_editCallbacks{};
// Never destroyed: removing a hook while a game thread may be inside it is not worth the risk, and
// the process ends with the game.
std::array<safetyhook::MidHook*, kMaxMidHooks> g_hooks{};
std::mutex g_installMutex;
int g_count = 0;
std::array<safetyhook::InlineHook*, kMaxInlineHooks> g_inlineHooks{};
int g_inlineCount = 0;

template <int N>
void thunk(safetyhook::Context& ctx) {
    HookRegisters registers{ctx.rax, ctx.rbx, ctx.rcx, ctx.rdx, ctx.rsi, ctx.rdi, ctx.rbp, ctx.rsp,
                            ctx.r8,  ctx.r9,  ctx.r10, ctx.r11, ctx.r12, ctx.r13, ctx.r14, ctx.r15};
    if (const MidHookCallback callback = g_callbacks[N].load(std::memory_order_acquire)) {
        callback(registers);
        return;
    }
    if (const MidHookEditCallback edit = g_editCallbacks[N].load(std::memory_order_acquire)) {
        edit(registers);
        ctx.rax = registers.rax;
        ctx.rbx = registers.rbx;
        ctx.rcx = registers.rcx;
        ctx.rdx = registers.rdx;
        ctx.rsi = registers.rsi;
        ctx.rdi = registers.rdi;
        ctx.rbp = registers.rbp;
        ctx.r8 = registers.r8;
        ctx.r9 = registers.r9;
        ctx.r10 = registers.r10;
        ctx.r11 = registers.r11;
        ctx.r12 = registers.r12;
        ctx.r13 = registers.r13;
        ctx.r14 = registers.r14;
        ctx.r15 = registers.r15;
    }
}

template <std::size_t... I>
constexpr std::array<safetyhook::MidHookFn, sizeof...(I)> makeThunks(std::index_sequence<I...>) {
    return {&thunk<static_cast<int>(I)>...};
}

constexpr auto kThunks = makeThunks(std::make_index_sequence<kMaxMidHooks>{});

bool pinSelf() {
    HMODULE self = nullptr;
    return GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
                              reinterpret_cast<LPCWSTR>(&pinSelf), &self) != FALSE;
}

} // namespace

namespace {

bool install(void* target, MidHookCallback callback, MidHookEditCallback edit, std::string& error);

} // namespace

bool installMidHook(void* target, MidHookCallback callback, std::string& error) {
    return install(target, callback, nullptr, error);
}

bool installMidHookEdit(void* target, MidHookEditCallback callback, std::string& error) {
    return install(target, nullptr, callback, error);
}

bool installInlineHook(void* target, void* destination, void** original, std::string& error) {
    std::lock_guard lock(g_installMutex);
    if (g_inlineCount >= kMaxInlineHooks) {
        error = "no free inline hook slot";
        return false;
    }
    if (!pinSelf()) {
        error = "cannot pin the layer DLL";
        return false;
    }
    // Created disabled: the trampoline must be handed out before a game thread can reach the detour.
    auto created = safetyhook::InlineHook::create(target, destination, safetyhook::InlineHook::StartDisabled);
    if (!created) {
        error = "inline hook failed (type " + std::to_string(static_cast<int>(created.error().type)) + ")";
        return false;
    }
    auto* hook = new safetyhook::InlineHook(std::move(*created));
    *original = hook->original<void*>();
    if (!hook->enable()) {
        *original = nullptr;
        delete hook; // never enabled: the game's code is unchanged
        error = "inline hook could not be enabled";
        return false;
    }
    g_inlineHooks[static_cast<std::size_t>(g_inlineCount++)] = hook;
    return true;
}

namespace {

bool install(void* target, MidHookCallback callback, MidHookEditCallback edit, std::string& error) {
    std::lock_guard lock(g_installMutex);
    if (g_count >= kMaxMidHooks) {
        error = "no free hook slot";
        return false;
    }
    // The hook's code lives in this DLL; it must never be unloaded while the game can reach it.
    if (!pinSelf()) {
        error = "cannot pin the layer DLL";
        return false;
    }
    const int slot = g_count;
    g_callbacks[slot].store(callback, std::memory_order_release);
    g_editCallbacks[slot].store(edit, std::memory_order_release);
    auto created = safetyhook::MidHook::create(target, kThunks[static_cast<std::size_t>(slot)]);
    if (!created) {
        g_callbacks[slot].store(nullptr);
        g_editCallbacks[slot].store(nullptr);
        const auto& e = created.error();
        error = e.type == safetyhook::MidHook::Error::BAD_ALLOCATION
                    ? "allocation near the target failed"
                    : "inline hook failed (type " +
                          std::to_string(static_cast<int>(e.inline_hook_error.type)) + ")";
        return false;
    }
    g_hooks[slot] = new safetyhook::MidHook(std::move(*created));
    ++g_count;
    return true;
}

} // namespace

} // namespace evr::vkcore
