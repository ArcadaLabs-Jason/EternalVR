#pragma once

// Replacing one of the game executable's USER32 imports (its import address table slot): the key injection,
// the desktop cursor guard and the render size's client area answers use it.

#include <windows.h>

#include <atomic>

namespace evr::vkcore {

// Replaces the exe's USER32 import `name` with `replacement`. Returns the function the slot held (another
// hook's thunk, if any), or nullptr when the import is not found or the slot cannot be written;
// `slotAddress`, when given, receives the address of the slot (what the game's `call [rip + disp]` reads).
void* patchImport(const char* name, void* replacement, const void** slotAddress = nullptr);

// The USER32 import `name` replaced by `replacement`; `original` receives the call to pass on to before the
// slot is switched, so a call racing the patch never sees null. False when it could not be replaced.
template <typename Fn>
bool replaceImport(const char* name,
                   std::atomic<Fn>& original,
                   Fn replacement,
                   const void** slotAddress = nullptr) {
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    auto* direct = user32 ? reinterpret_cast<Fn>(GetProcAddress(user32, name)) : nullptr;
    if (!direct) {
        return false;
    }
    original.store(direct);
    void* previous = patchImport(name, reinterpret_cast<void*>(replacement), slotAddress);
    if (!previous) {
        return false;
    }
    original.store(reinterpret_cast<Fn>(previous));
    return true;
}

} // namespace evr::vkcore
