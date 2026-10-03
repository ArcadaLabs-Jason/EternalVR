#include "vkcore/import_patch.hpp"

#include <cstddef>
#include <cstring>
#include <mutex>

namespace evr::vkcore {

namespace {

// Every write to a read-only slot of the exe: an unprotect, write, protect sequence on another slot of the
// same page must not run in between.
std::mutex g_slotMutex;

} // namespace

bool writeReadOnlySlot(void* slot, void* value, void** previous) {
    const std::lock_guard lock(g_slotMutex);
    DWORD old = 0;
    if (!VirtualProtect(slot, sizeof(void*), PAGE_READWRITE, &old)) {
        return false;
    }
    void* held = InterlockedExchangePointer(static_cast<void* volatile*>(slot), value);
    VirtualProtect(slot, sizeof(void*), old, &old);
    if (previous) {
        *previous = held;
    }
    return true;
}

void* patchImport(const char* name, void* replacement, const void** slotAddress, const char* dll) {
    auto* module = reinterpret_cast<std::byte*>(GetModuleHandleW(nullptr));
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(module);
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(module + dos->e_lfanew);
    const IMAGE_DATA_DIRECTORY& dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (!dir.VirtualAddress) {
        return nullptr;
    }
    for (auto* desc = reinterpret_cast<const IMAGE_IMPORT_DESCRIPTOR*>(module + dir.VirtualAddress);
         desc->Name; ++desc) {
        if (_stricmp(reinterpret_cast<const char*>(module + desc->Name), dll) != 0 ||
            !desc->OriginalFirstThunk) {
            continue;
        }
        auto* names = reinterpret_cast<const IMAGE_THUNK_DATA64*>(module + desc->OriginalFirstThunk);
        auto* slots = reinterpret_cast<IMAGE_THUNK_DATA64*>(module + desc->FirstThunk);
        for (; names->u1.AddressOfData; ++names, ++slots) {
            if (IMAGE_SNAP_BY_ORDINAL64(names->u1.Ordinal)) {
                continue;
            }
            const auto* byName =
                reinterpret_cast<const IMAGE_IMPORT_BY_NAME*>(module + names->u1.AddressOfData);
            if (std::strcmp(reinterpret_cast<const char*>(byName->Name), name) != 0) {
                continue;
            }
            void* original = nullptr;
            if (!writeReadOnlySlot(&slots->u1.Function, replacement, &original)) {
                return nullptr;
            }
            if (slotAddress) {
                *slotAddress = &slots->u1.Function;
            }
            return original;
        }
    }
    return nullptr;
}

} // namespace evr::vkcore
