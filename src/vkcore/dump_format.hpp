#pragma once

// Pure helpers for the shader and draw dump (shader_dump.cpp): hashing, JSON text, the frame window and
// pNext walks. Kept apart from the layer so they are unit-tested without a device.

#include <vulkan/vulkan.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace evr::vkcore::dump {

// FNV-1a, 64 bit, over raw bytes. Names a SPIR-V module on disk (<hash>.spv).
std::uint64_t fnv1a64(const void* data, std::size_t size);

// 16 lower-case hex digits, zero padded.
std::string hex64(std::uint64_t value);

// Lower-case hex of each byte, in order, no separators.
std::string hexBytes(const void* data, std::size_t size);

// "0x" plus lower-case hex, no padding; handles and pointers in the logs.
std::string handleText(std::uint64_t handle);

template <typename Handle>
std::uint64_t handleValue(Handle handle) {
    return static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(handle));
}

// Appends `text` as a quoted JSON string (quotes, backslashes and control characters escaped).
void appendJsonString(std::string& out, std::string_view text);

// Parses a decimal count from an environment value; `fallback` when empty or not a number.
std::uint32_t parseCount(std::wstring_view text, std::uint32_t fallback);

// The frames whose draws are logged: presents [skip, skip + count), counted from the first present.
class FrameWindow {
public:
    FrameWindow(std::uint32_t skip, std::uint32_t count) : skip_(skip), count_(count) {}

    bool contains(std::uint64_t frame) const { return frame >= skip_ && frame - skip_ < count_; }
    // True once every frame of the window has been presented.
    bool finished(std::uint64_t frame) const { return frame >= static_cast<std::uint64_t>(skip_) + count_; }
    std::uint32_t skip() const { return skip_; }
    std::uint32_t count() const { return count_; }

private:
    std::uint32_t skip_;
    std::uint32_t count_;
};

// The first structure of the given type in a pNext chain, or nullptr.
const VkBaseInStructure* findInChain(const void* pNext, VkStructureType type);

template <typename T>
const T* findInChain(const void* pNext, VkStructureType type) {
    return reinterpret_cast<const T*>(findInChain(pNext, type));
}

} // namespace evr::vkcore::dump
