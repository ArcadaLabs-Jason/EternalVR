#include "vkcore/game_text.hpp"

#include "vkcore/log.hpp"

#include <windows.h>

#include <cstring>

namespace evr::vkcore {

namespace {

resolver::ByteSpan sectionSpan(const std::byte* module, const IMAGE_NT_HEADERS64* nt, const char* name) {
    const IMAGE_SECTION_HEADER* section = IMAGE_FIRST_SECTION(nt);
    for (WORD i = 0; i < nt->FileHeader.NumberOfSections; ++i, ++section) {
        char sectionName[IMAGE_SIZEOF_SHORT_NAME + 1] = {};
        std::memcpy(sectionName, section->Name, IMAGE_SIZEOF_SHORT_NAME);
        if (std::strcmp(sectionName, name) == 0) {
            return resolver::ByteSpan(module + section->VirtualAddress, section->Misc.VirtualSize);
        }
    }
    return {};
}

} // namespace

bool locateGameImage(GameImage& image, const char* tag) {
    auto* module = reinterpret_cast<const std::byte*>(GetModuleHandleW(nullptr));
    if (!module) {
        return false;
    }
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(module);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) {
        return false;
    }
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(module + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE || nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC) {
        return false;
    }
    image.base = module;
    image.size = nt->OptionalHeader.SizeOfImage;
    image.timestamp = nt->FileHeader.TimeDateStamp;
    image.text = sectionSpan(module, nt, ".text");
    image.rdata = sectionSpan(module, nt, ".rdata");
    if (image.text.empty()) {
        return false;
    }
    EVR_LOG("%s: game module at %p, .text %u KiB, timestamp 0x%08lx", tag, static_cast<const void*>(module),
            static_cast<unsigned>(image.text.size() / 1024), static_cast<unsigned long>(image.timestamp));
    return true;
}

const std::byte*
findUnique(const GameImage& image, const char* tag, const char* name, const char* signature) {
    auto pattern = resolver::Pattern::parse(signature);
    if (!pattern) {
        EVR_LOG("%s: %s signature does not parse", tag, name);
        return nullptr;
    }
    const auto matches = resolver::findAll(image.text, *pattern);
    if (matches.size() != 1) {
        EVR_LOG("%s: %s signature matched %zu time(s), expected 1", tag, name, matches.size());
        return nullptr;
    }
    const std::byte* at = image.text.data() + matches.front();
    EVR_LOG("%s: %s at RVA 0x%X", tag, name, image.rva(at));
    return at;
}

std::int32_t readI32(const std::byte* at) {
    std::int32_t value = 0;
    std::memcpy(&value, at, sizeof(value));
    return value;
}

const std::byte*
ripTarget(const GameImage& image, const std::byte* dispAt, const std::byte* nextInstruction) {
    if (!image.contains(dispAt, 4) || !image.contains(nextInstruction)) {
        return nullptr;
    }
    const std::intptr_t address = reinterpret_cast<std::intptr_t>(nextInstruction) + readI32(dispAt);
    const auto* target = reinterpret_cast<const std::byte*>(address);
    return image.contains(target) ? target : nullptr;
}

bool matchesAt(const GameImage& image, const std::byte* at, const char* signature) {
    auto pattern = resolver::Pattern::parse(signature);
    return pattern && at && image.inText(at, pattern->size()) &&
           pattern->matchesAt(image.text, static_cast<std::size_t>(at - image.text.data()));
}

const std::byte* branchTarget(const GameImage& image, const std::byte* at) {
    if (!image.inText(at, 6)) {
        return nullptr;
    }
    const auto opcode = std::to_integer<unsigned>(at[0]);
    const std::byte* target = nullptr;
    if (opcode == 0xE8 || opcode == 0xE9) {
        target = at + 5 + readI32(at + 1);
    } else if (opcode == 0x0F && (std::to_integer<unsigned>(at[1]) & 0xF0) == 0x80) {
        target = at + 6 + readI32(at + 2);
    }
    return target && image.inText(target) ? target : nullptr;
}

std::string_view stringAt(const GameImage& image, const std::byte* p, std::size_t maxLength) {
    if (!image.contains(p)) {
        return {};
    }
    const std::size_t available = image.size - static_cast<std::size_t>(p - image.base);
    const std::size_t limit = maxLength < available ? maxLength : available;
    const void* end = std::memchr(p, 0, limit);
    if (!end) {
        return {};
    }
    return std::string_view(reinterpret_cast<const char*>(p),
                            static_cast<std::size_t>(static_cast<const std::byte*>(end) - p));
}

std::string_view rttiName(const GameImage& image, const std::byte* vtable) {
    // vtable[-1] is the complete-object locator: signature 1 (x64), offset, cdOffset, type descriptor
    // RVA, class descriptor RVA, its own RVA. The type descriptor's name follows two pointers.
    if (!image.contains(vtable - 8, 8)) {
        return {};
    }
    std::uintptr_t locatorVa = 0;
    std::memcpy(&locatorVa, vtable - 8, sizeof(locatorVa));
    const auto* locator = reinterpret_cast<const std::byte*>(locatorVa);
    if (!image.contains(locator, 24)) {
        return {};
    }
    std::uint32_t fields[6];
    std::memcpy(fields, locator, sizeof(fields));
    if (fields[0] != 1 || fields[5] != image.rva(locator) || fields[3] >= image.size) {
        return {};
    }
    return stringAt(image, image.base + fields[3] + 16, 512);
}

const std::byte* functionStart(const GameImage& image, const std::byte* code) {
    if (!image.inText(code)) {
        return nullptr;
    }
    DWORD64 imageBase = 0;
    const RUNTIME_FUNCTION* entry =
        RtlLookupFunctionEntry(reinterpret_cast<DWORD64>(code), &imageBase, nullptr);
    if (!entry || reinterpret_cast<const std::byte*>(imageBase) != image.base) {
        return nullptr;
    }
    // Chained unwind info (UNW_FLAG_CHAININFO) names the entry of the function this chunk belongs to.
    for (int depth = 0; depth < 32; ++depth) {
        const std::byte* unwind = image.base + entry->UnwindData;
        if (!image.contains(unwind, 4)) {
            return nullptr;
        }
        const auto versionFlags = std::to_integer<unsigned>(unwind[0]);
        if (((versionFlags >> 3) & UNW_FLAG_CHAININFO) == 0) {
            return image.base + entry->BeginAddress;
        }
        const auto codes = std::to_integer<unsigned>(unwind[2]);
        const std::byte* chained = unwind + 4 + ((codes + 1u) & ~1u) * 2u;
        if (!image.contains(chained, sizeof(RUNTIME_FUNCTION))) {
            return nullptr;
        }
        entry = reinterpret_cast<const RUNTIME_FUNCTION*>(chained);
    }
    return nullptr;
}

const std::byte* findUniqueString(const GameImage& image, std::string_view text) {
    if (image.rdata.empty() || text.empty()) {
        return nullptr;
    }
    const auto* data = reinterpret_cast<const char*>(image.rdata.data());
    const std::string_view haystack(data, image.rdata.size());
    const std::byte* found = nullptr;
    for (std::size_t at = haystack.find(text); at != std::string_view::npos;
         at = haystack.find(text, at + 1)) {
        const std::size_t end = at + text.size();
        if (end >= haystack.size() || haystack[end] != '\0' || (at > 0 && haystack[at - 1] != '\0')) {
            continue;
        }
        if (found) {
            return nullptr; // more than one copy
        }
        found = image.rdata.data() + at;
    }
    return found;
}

std::vector<const std::byte*> findLeaReferences(const GameImage& image, const std::byte* target) {
    std::vector<const std::byte*> hits;
    const std::byte* text = image.text.data();
    const std::size_t n = image.text.size();
    const auto wanted = reinterpret_cast<std::intptr_t>(target);
    for (std::size_t i = 0; i + 7 <= n; ++i) {
        const auto rex = std::to_integer<unsigned>(text[i]);
        if ((rex != 0x48 && rex != 0x4C) || text[i + 1] != std::byte{0x8D} ||
            (std::to_integer<unsigned>(text[i + 2]) & 0xC7u) != 0x05u) {
            continue;
        }
        if (reinterpret_cast<std::intptr_t>(text + i + 7) + readI32(text + i + 3) == wanted) {
            hits.push_back(text + i);
        }
    }
    return hits;
}

std::vector<std::uintptr_t>
callReturns(const GameImage& image, const std::byte* function, const std::byte* target, std::size_t span) {
    std::vector<std::uintptr_t> found;
    for (std::size_t i = 0; i + 5 <= span && image.inText(function + i, 5); ++i) {
        const std::byte* at = function + i;
        if (at[0] == std::byte{0xE8} && at + 5 + readI32(at + 1) == target) {
            found.push_back(reinterpret_cast<std::uintptr_t>(at + 5));
        }
    }
    return found;
}

std::pair<const std::byte*, std::size_t> dataSection(const GameImage& image) {
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(image.base);
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(image.base + dos->e_lfanew);
    const IMAGE_SECTION_HEADER* section = IMAGE_FIRST_SECTION(nt);
    for (unsigned i = 0; i < nt->FileHeader.NumberOfSections; ++i, ++section) {
        if (std::memcmp(section->Name, ".data\0\0\0", 8) == 0 &&
            image.contains(image.base + section->VirtualAddress, section->Misc.VirtualSize)) {
            return {image.base + section->VirtualAddress, section->Misc.VirtualSize};
        }
    }
    return {nullptr, 0};
}

} // namespace evr::vkcore
