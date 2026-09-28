#pragma once

// The set of saved profiles and the operations the launcher offers on them: create, duplicate,
// rename and delete (ARCHITECTURE section 12). Persistence lives elsewhere; this is the in-memory
// model it loads into and saves from.
//
// Profile names become file names, and Windows file names are case-insensitive, so names are
// compared ignoring ASCII case and must be usable as a file name on every platform: see
// ProfileError for what is refused. Surrounding whitespace is trimmed off before any check.

#include "common/result.hpp"
#include "platform/settings/preset.hpp"
#include "platform/settings/profile.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace evr::settings {

enum class ProfileError : std::uint8_t {
    // Empty or only whitespace.
    EmptyName,
    // A character that is not allowed in file names (\ / : * ? " < > | or a control character), a
    // trailing dot, or a device name Windows reserves (CON, NUL, COM1 and so on, with or without an
    // extension).
    InvalidName,
    // Another profile has the same name, ignoring case.
    NameTaken,
    NotFound,
    // The base preset is not registered.
    UnknownPreset,
};

class ProfileStore {
public:
    // `presets` must outlive the store; new profiles must be based on one of them.
    explicit ProfileStore(const PresetRegistry& presets) : presets_(&presets) {}

    // Adds a new profile with no overrides.
    Result<Profile*, ProfileError> create(std::string_view name, std::string_view basePreset);

    // Copies an existing profile, overrides included, under a new name.
    Result<Profile*, ProfileError> duplicate(std::string_view sourceName, std::string_view newName);

    // Renaming to the current name, or to a different capitalisation of it, is allowed.
    Result<Profile*, ProfileError> rename(std::string_view oldName, std::string_view newName);

    // Returns false if no profile has that name.
    bool remove(std::string_view name);

    // Looks a profile up by name, ignoring ASCII case and surrounding whitespace.
    [[nodiscard]] Profile* find(std::string_view name);
    [[nodiscard]] const Profile* find(std::string_view name) const;

    // Names in creation order.
    [[nodiscard]] std::vector<std::string> names() const;

private:
    // Checks that `name` can be given to a profile other than `self`, returning the trimmed name.
    [[nodiscard]] Result<std::string, ProfileError> checkNewName(std::string_view name,
                                                                 const Profile* self = nullptr) const;

    const PresetRegistry* presets_;
    // A vector rather than a map keeps creation order stable for the launcher's list. Profile counts
    // are small, so linear lookup is fine. Pointers returned by the API are invalidated by create,
    // duplicate and remove.
    std::vector<Profile> profiles_;
};

} // namespace evr::settings
