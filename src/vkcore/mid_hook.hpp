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
    // An edit callback may set this to the game's address to resume at instead of the hooked instruction
    // (skipping it and what follows up to there); 0 resumes as normal.
    std::uintptr_t resumeAt = 0;
};

// A register by its x86 number (rax rcx rdx rbx rsp rbp rsi rdi r8..r15), as instruction encodings name them.
inline std::uintptr_t& registerByNumber(HookRegisters& r, int number) {
    std::uintptr_t* const byNumber[16] = {&r.rax, &r.rcx, &r.rdx, &r.rbx, &r.rsp, &r.rbp, &r.rsi, &r.rdi,
                                          &r.r8,  &r.r9,  &r.r10, &r.r11, &r.r12, &r.r13, &r.r14, &r.r15};
    return *byNumber[number & 15];
}

using MidHookCallback = void (*)(const HookRegisters& registers);
// A callback that may change the registers: every one but rsp is written back before the game resumes.
using MidHookEditCallback = void (*)(HookRegisters& registers);

// Installs a hook at `target` that calls `callback`. Hooks stay installed for the life of the process
// (the layer DLL is pinned first). At most kMaxMidHooks hooks exist. On failure `error` says why and
// the game's code is left untouched. Sized at about twice the most a session can use: 48 measured with
// bHaptics and the free off hand (debug commands add more), plus Parallel Eye Rendering's 168 (its view
// slots, redirects, binning edges and view 1's clones), so a new feature does not quietly push an older one
// out.
inline constexpr int kMaxMidHooks = 448;
bool installMidHook(void* target, MidHookCallback callback, std::string& error);
bool installMidHookEdit(void* target, MidHookEditCallback callback, std::string& error);

// Detours the function at `target` to `destination`, which has the same signature; `original` receives a
// trampoline that runs the game's own function. Installed for the life of the process like the mid hooks;
// at most kMaxInlineHooks (about twice the 13 measured with the launcher's newer DLSS DLL plus Parallel Eye
// Rendering's 9). On failure `error` says why and the game's code is left untouched.
inline constexpr int kMaxInlineHooks = 48;
bool installInlineHook(void* target, void* destination, void** original, std::string& error);

// How many hooks of each kind are installed, for the log.
int midHookCount();
int inlineHookCount();

} // namespace evr::vkcore
