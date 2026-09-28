#pragma once

// Mid-function hooks on the game's code, through safetyhook (THIRD_PARTY_NOTICES.md).
//
// A mid hook runs a callback at an arbitrary instruction with the thread's integer registers as they
// are at that point, then resumes the original code. The callbacks here only read registers; they
// change the game's data through the pointers those registers hold.
//
// This header keeps safetyhook (which needs C++23) out of the rest of the layer.

#include <cstdint>
#include <string>

namespace evr::vkcore {

struct HookRegisters {
    std::uintptr_t rax, rbx, rcx, rdx, rsi, rdi, rbp, rsp;
    std::uintptr_t r8, r9, r10, r11, r12, r13, r14, r15;
};

using MidHookCallback = void (*)(const HookRegisters& registers);
// A callback that may change the registers: every one but rsp is written back before the game resumes.
using MidHookEditCallback = void (*)(HookRegisters& registers);

// Installs a hook at `target` that calls `callback`. Hooks stay installed for the life of the process
// (the layer DLL is pinned first). At most kMaxMidHooks hooks exist. On failure `error` says why and
// the game's code is left untouched.
inline constexpr int kMaxMidHooks = 48;
bool installMidHook(void* target, MidHookCallback callback, std::string& error);
bool installMidHookEdit(void* target, MidHookEditCallback callback, std::string& error);

// Detours the function at `target` to `destination`, which has the same signature; `original` receives a
// trampoline that runs the game's own function. Installed for the life of the process like the mid hooks;
// at most kMaxInlineHooks. On failure `error` says why and the game's code is left untouched.
inline constexpr int kMaxInlineHooks = 8;
bool installInlineHook(void* target, void* destination, void** original, std::string& error);

} // namespace evr::vkcore
