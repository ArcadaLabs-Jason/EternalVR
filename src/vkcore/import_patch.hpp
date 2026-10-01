#pragma once

// Replacing one of the game executable's imports (its import address table slot): the key injection, the
// desktop cursor guard and the render size's client area answers use USER32's.

#include <windows.h>

#include <atomic>

namespace evr::vkcore {

// Replaces the exe's import `name` from `dll` ("USER32.dll") with `replacement`. Returns the function the
// slot held (another hook's thunk, if any), or nullptr when the import is not found or the slot cannot be
// written; `slotAddress`, when given, receives the address of the slot (what the game's `call [rip + disp]`
// reads).
void* patchImport(const char* name,
                  void* replacement,
                  const void** slotAddress = nullptr,
                  const char* dll = "USER32.dll");

// The import `name` from `dll` replaced by `replacement`; `original` receives the call to pass on to before
// the slot is switched, so a call racing the patch never sees null. False when it could not be replaced.
template <typename Fn>
bool replaceImport(const char* name,
                   std::atomic<Fn>& original,
                   Fn replacement,
                   const void** slotAddress = nullptr,
                   const char* dll = "USER32.dll") {
    HMODULE module = GetModuleHandleA(dll);
    auto* direct = module ? reinterpret_cast<Fn>(GetProcAddress(module, name)) : nullptr;
    if (!direct) {
        return false;
    }
    original.store(direct);
    void* previous = patchImport(name, reinterpret_cast<void*>(replacement), slotAddress, dll);
    if (!previous) {
        return false;
    }
    original.store(reinterpret_cast<Fn>(previous));
    return true;
}

} // namespace evr::vkcore
