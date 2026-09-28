#pragma once

// Presets: named bundles of values that seed profiles (ARCHITECTURE sections 10 and 12).
//
// A preset only lists the keys it has an opinion about. Anything it leaves out falls through to the
// schema default, so adding a new setting never requires touching every preset.

#include "common/result.hpp"
#include "platform/settings/schema.hpp"
#include "platform/settings/setting_value.hpp"

#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace evr::settings {

struct Preset {
    std::string name;
    std::map<std::string, SettingValue, std::less<>> values;
};

enum class PresetError : std::uint8_t {
    NameTaken,
    UnknownKey,   // A value for a key the schema does not have.
    InvalidValue, // A value that does not validate cleanly: wrong type, out of range, bad choice.
};

class PresetRegistry {
public:
    // Registers `preset` after checking every value against `schema`. Presets are part of the
    // program, so a value that the schema would clamp or convert is refused as well: a preset must
    // say exactly what it means. Values are stored in the schema's storage type (an integer given
    // for a Float setting is stored as a float). Nothing is registered on error. The pointer is
    // invalidated by the next add.
    Result<const Preset*, PresetError> add(const Schema& schema, Preset preset);

    [[nodiscard]] const Preset* find(std::string_view name) const;

    // Names in registration order, which is the order the launcher lists them in.
    [[nodiscard]] std::vector<std::string> names() const;

private:
    std::vector<Preset> presets_;
};

} // namespace evr::settings
