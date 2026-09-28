#include "vkcore/import_patch.hpp"

#include <cstddef>
#include <cstring>

namespace evr::vkcore {

void* patchImport(const char* name, void* replacement, const void** slotAddress) {
    auto* module = reinterpret_cast<std::byte*>(GetModuleHandleW(nullptr));
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(module);
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(module + dos->e_lfanew);
    const IMAGE_DATA_DIRECTORY& dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (!dir.VirtualAddress) {
        return nullptr;
    }
    for (auto* desc = reinterpret_cast<const IMAGE_IMPORT_DESCRIPTOR*>(module + dir.VirtualAddress);
         desc->Name; ++desc) {
        if (_stricmp(reinterpret_cast<const char*>(module + desc->Name), "USER32.dll") != 0 ||
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
            DWORD old = 0;
            if (!VirtualProtect(&slots->u1.Function, sizeof(slots->u1.Function), PAGE_READWRITE, &old)) {
                return nullptr;
            }
            void* original = reinterpret_cast<void*>(slots->u1.Function);
            slots->u1.Function = reinterpret_cast<ULONGLONG>(replacement);
            VirtualProtect(&slots->u1.Function, sizeof(slots->u1.Function), old, &old);
            if (slotAddress) {
                *slotAddress = &slots->u1.Function;
            }
            return original;
        }
    }
    return nullptr;
}

} // namespace evr::vkcore
