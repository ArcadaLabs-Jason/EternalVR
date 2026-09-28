#pragma once

// Whether the Vulkan layer should activate in the current process (T-079, T-109). Portable so the
// rules can be tested without Windows; the layer passes in the exe path and the two variables.

#include <optional>
#include <string_view>

namespace evr::layer_gate {

inline constexpr std::wstring_view kTargetExe = L"DOOMEternalx64vk.exe";

enum class Decision {
    Enable,
    WrongProcess, // not DOOMEternalx64vk.exe
    NotEnabled,   // ETERNALVR_ENABLE_LAYER is not "1"
    Disabled,     // ETERNALVR_DISABLE_LAYER is set to a non-empty value
};

// Base name of a Windows or POSIX path: everything after the last '\' or '/'.
std::wstring_view baseName(std::wstring_view path);

// True when the exe path names DOOMEternalx64vk.exe, compared case-insensitively (ASCII).
bool isTargetExecutable(std::wstring_view exePath);

// A missing variable is std::nullopt. Any non-empty disable value wins, as the loader treats
// disable_environment; enable must be exactly "1".
Decision decide(std::wstring_view exePath,
                std::optional<std::wstring_view> enableValue,
                std::optional<std::wstring_view> disableValue);

const char* toString(Decision decision);

} // namespace evr::layer_gate
