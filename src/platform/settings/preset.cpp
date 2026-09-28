#include "platform/settings/preset.hpp"

#include <algorithm>
#include <utility>

namespace evr::settings {

Result<const Preset*, PresetError> PresetRegistry::add(const Schema& schema, Preset preset) {
    if (find(preset.name) != nullptr) {
        return fail(PresetError::NameTaken, "a preset named '" + preset.name + "' already exists");
    }
    for (auto& [key, value] : preset.values) {
        const Validation validation = schema.validate(key, value);
        if (validation.status == ValidationStatus::UnknownKey) {
            return fail(PresetError::UnknownKey, "preset '" + preset.name + "': " + validation.message);
        }
        if (validation.status != ValidationStatus::Ok) {
            const std::string reason = validation.status == ValidationStatus::Clamped
                                           ? key + ": value is out of range"
                                           : validation.message;
            return fail(PresetError::InvalidValue, "preset '" + preset.name + "': " + reason);
        }
        value = validation.value;
    }
    presets_.push_back(std::move(preset));
    return &presets_.back();
}

const Preset* PresetRegistry::find(std::string_view name) const {
    const auto it = std::find_if(presets_.begin(), presets_.end(),
                                 [name](const Preset& preset) { return preset.name == name; });
    return (it != presets_.end()) ? &*it : nullptr;
}

std::vector<std::string> PresetRegistry::names() const {
    std::vector<std::string> result;
    result.reserve(presets_.size());
    for (const Preset& preset : presets_) {
        result.push_back(preset.name);
    }
    return result;
}

} // namespace evr::settings
