#include "vkcore/dump_format.hpp"

#include <cstdint>
#include <string>

namespace evr::vkcore::dump {

namespace {

constexpr char kHex[] = "0123456789abcdef";

} // namespace

std::uint64_t fnv1a64(const void* data, std::size_t size) {
    std::uint64_t hash = 0xcbf29ce484222325ull;
    const auto* bytes = static_cast<const unsigned char*>(data);
    for (std::size_t i = 0; i < size; ++i) {
        hash ^= bytes[i];
        hash *= 0x100000001b3ull;
    }
    return hash;
}

std::string hex64(std::uint64_t value) {
    std::string out(16, '0');
    for (int i = 15; i >= 0; --i) {
        out[static_cast<std::size_t>(i)] = kHex[value & 0xF];
        value >>= 4;
    }
    return out;
}

std::string hexBytes(const void* data, std::size_t size) {
    std::string out;
    out.reserve(size * 2);
    const auto* bytes = static_cast<const unsigned char*>(data);
    for (std::size_t i = 0; i < size; ++i) {
        out.push_back(kHex[bytes[i] >> 4]);
        out.push_back(kHex[bytes[i] & 0xF]);
    }
    return out;
}

std::string handleText(std::uint64_t handle) {
    if (handle == 0) {
        return "0x0";
    }
    std::string digits;
    while (handle) {
        digits.insert(digits.begin(), kHex[handle & 0xF]);
        handle >>= 4;
    }
    return "0x" + digits;
}

void appendJsonString(std::string& out, std::string_view text) {
    out.push_back('"');
    for (const char c : text) {
        const auto u = static_cast<unsigned char>(c);
        if (c == '"' || c == '\\') {
            out.push_back('\\');
            out.push_back(c);
        } else if (u < 0x20) {
            out += "\\u00";
            out.push_back(kHex[u >> 4]);
            out.push_back(kHex[u & 0xF]);
        } else {
            out.push_back(c);
        }
    }
    out.push_back('"');
}

std::uint32_t parseCount(std::wstring_view text, std::uint32_t fallback) {
    if (text.empty() || text.size() > 9) {
        return fallback;
    }
    std::uint32_t value = 0;
    for (const wchar_t c : text) {
        if (c < L'0' || c > L'9') {
            return fallback;
        }
        value = value * 10 + static_cast<std::uint32_t>(c - L'0');
    }
    return value;
}

const VkBaseInStructure* findInChain(const void* pNext, VkStructureType type) {
    for (auto* node = static_cast<const VkBaseInStructure*>(pNext); node; node = node->pNext) {
        if (node->sType == type) {
            return node;
        }
    }
    return nullptr;
}

} // namespace evr::vkcore::dump
