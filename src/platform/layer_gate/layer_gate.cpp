#include "platform/layer_gate/layer_gate.hpp"

#include <cstddef>

namespace evr::layer_gate {

namespace {

wchar_t asciiLower(wchar_t c) {
    return (c >= L'A' && c <= L'Z') ? static_cast<wchar_t>(c - L'A' + L'a') : c;
}

bool equalsIgnoreCase(std::wstring_view a, std::wstring_view b) {
    if (a.size() != b.size()) {
        return false;
    }
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (asciiLower(a[i]) != asciiLower(b[i])) {
            return false;
        }
    }
    return true;
}

} // namespace

std::wstring_view baseName(std::wstring_view path) {
    const auto slash = path.find_last_of(L"\\/");
    return slash == std::wstring_view::npos ? path : path.substr(slash + 1);
}

bool isTargetExecutable(std::wstring_view exePath) {
    return equalsIgnoreCase(baseName(exePath), kTargetExe);
}

Decision decide(std::wstring_view exePath,
                std::optional<std::wstring_view> enableValue,
                std::optional<std::wstring_view> disableValue) {
    if (disableValue && !disableValue->empty()) {
        return Decision::Disabled;
    }
    if (!enableValue || *enableValue != L"1") {
        return Decision::NotEnabled;
    }
    if (!isTargetExecutable(exePath)) {
        return Decision::WrongProcess;
    }
    return Decision::Enable;
}

const char* toString(Decision decision) {
    switch (decision) {
    case Decision::Enable:
        return "enable";
    case Decision::WrongProcess:
        return "wrong process";
    case Decision::NotEnabled:
        return "not enabled";
    case Decision::Disabled:
        return "disabled";
    }
    return "unknown";
}

} // namespace evr::layer_gate
