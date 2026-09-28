#include "platform/settings/setting_value.hpp"

#include <sstream>
#include <type_traits>

namespace evr::settings {

bool holdsStorageFor(SettingType type, const SettingValue& value) {
    switch (type) {
    case SettingType::Bool:
        return std::holds_alternative<bool>(value);
    case SettingType::Int:
        return std::holds_alternative<std::int64_t>(value);
    case SettingType::Float:
        return std::holds_alternative<double>(value);
    case SettingType::String:
    case SettingType::Enum:
        return std::holds_alternative<std::string>(value);
    }
    return false;
}

std::string toDisplayString(const SettingValue& value) {
    return std::visit(
        [](const auto& v) -> std::string {
            using T = std::decay_t<decltype(v)>;
            if constexpr (std::is_same_v<T, bool>) {
                return v ? "true" : "false";
            } else if constexpr (std::is_same_v<T, std::string>) {
                return "\"" + v + "\"";
            } else {
                // ostringstream rather than std::to_string, which prints doubles with six fixed
                // decimals ("0.500000").
                std::ostringstream out;
                out << v;
                return out.str();
            }
        },
        value);
}

const char* toString(SettingType type) {
    switch (type) {
    case SettingType::Bool:
        return "bool";
    case SettingType::Int:
        return "int";
    case SettingType::Float:
        return "float";
    case SettingType::String:
        return "string";
    case SettingType::Enum:
        return "enum";
    }
    return "unknown";
}

} // namespace evr::settings
