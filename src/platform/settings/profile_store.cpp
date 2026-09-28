#include "platform/settings/profile_store.hpp"

#include "platform/settings/profile_name.hpp"

#include <algorithm>
#include <utility>

namespace evr::settings {

Result<std::string, ProfileError> ProfileStore::checkNewName(std::string_view name,
                                                             const Profile* self) const {
    const std::string_view trimmed = trimProfileName(name);
    if (trimmed.empty()) {
        return fail(ProfileError::EmptyName, "profile name must not be empty");
    }
    if (!isFileSafeProfileName(trimmed)) {
        return fail(ProfileError::InvalidName,
                    "'" + std::string(trimmed) +
                        "' cannot be a profile name: it must not contain \\ / : * ? \" < > | or control "
                        "characters, end in a dot, or be a reserved device name such as CON or NUL");
    }
    if (const Profile* existing = find(trimmed); existing != nullptr && existing != self) {
        return fail(ProfileError::NameTaken, "a profile named '" + existing->name + "' already exists");
    }
    return std::string(trimmed);
}

Result<Profile*, ProfileError> ProfileStore::create(std::string_view name, std::string_view basePreset) {
    const auto checked = checkNewName(name);
    if (!checked) {
        return checked.error();
    }
    if (presets_->find(basePreset) == nullptr) {
        return fail(ProfileError::UnknownPreset, "no preset named '" + std::string(basePreset) + "'");
    }
    profiles_.push_back(Profile{*checked, std::string(basePreset), {}});
    return &profiles_.back();
}

Result<Profile*, ProfileError> ProfileStore::duplicate(std::string_view sourceName,
                                                       std::string_view newName) {
    const Profile* source = find(sourceName);
    if (source == nullptr) {
        return fail(ProfileError::NotFound, "no profile named '" + std::string(sourceName) + "'");
    }
    const auto checked = checkNewName(newName);
    if (!checked) {
        return checked.error();
    }
    // Copy before push_back: growing the vector may invalidate `source`.
    Profile copy = *source;
    copy.name = *checked;
    profiles_.push_back(std::move(copy));
    return &profiles_.back();
}

Result<Profile*, ProfileError> ProfileStore::rename(std::string_view oldName, std::string_view newName) {
    Profile* profile = find(oldName);
    if (profile == nullptr) {
        return fail(ProfileError::NotFound, "no profile named '" + std::string(oldName) + "'");
    }
    const auto checked = checkNewName(newName, profile);
    if (!checked) {
        return checked.error();
    }
    profile->name = *checked;
    return profile;
}

bool ProfileStore::remove(std::string_view name) {
    const auto it = std::find_if(profiles_.begin(), profiles_.end(), [name](const Profile& profile) {
        return sameProfileName(profile.name, name);
    });
    if (it == profiles_.end()) {
        return false;
    }
    profiles_.erase(it);
    return true;
}

Profile* ProfileStore::find(std::string_view name) {
    // Reuse the const lookup; the store owns the profiles, so handing out a mutable pointer is safe.
    return const_cast<Profile*>(std::as_const(*this).find(name));
}

const Profile* ProfileStore::find(std::string_view name) const {
    const auto it = std::find_if(profiles_.begin(), profiles_.end(), [name](const Profile& profile) {
        return sameProfileName(profile.name, name);
    });
    return (it != profiles_.end()) ? &*it : nullptr;
}

std::vector<std::string> ProfileStore::names() const {
    std::vector<std::string> result;
    result.reserve(profiles_.size());
    for (const Profile& profile : profiles_) {
        result.push_back(profile.name);
    }
    return result;
}

} // namespace evr::settings
