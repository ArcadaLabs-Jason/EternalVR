#include "platform/settings/profile_store.hpp"

#include "platform/settings/builtin.hpp"

#include <doctest/doctest.h>

#include <cstdint>
#include <initializer_list>
#include <ostream>
#include <string>
#include <vector>

using evr::settings::ProfileError;
using evr::settings::ProfileStore;

namespace {

struct Fixture {
    evr::settings::BuiltinSettings builtins = evr::settings::makeBuiltinSettings();
    ProfileStore store{builtins.presets};
};

} // namespace

TEST_CASE("create adds profiles in order and rejects bad names") {
    Fixture f;
    ProfileStore& store = f.store;
    REQUIRE(store.create("Alice", "Comfortable").ok());
    REQUIRE(store.create("Bob", "Advanced").ok());
    CHECK(store.names() == std::vector<std::string>{"Alice", "Bob"});

    CHECK(store.create("Alice", "Intense").error().code == ProfileError::NameTaken);
    CHECK(store.create("", "Intense").error().code == ProfileError::EmptyName);
    CHECK(store.find("Alice")->basePreset == "Comfortable");
}

TEST_CASE("names differing only in case or surrounding whitespace are the same name") {
    Fixture f;
    ProfileStore& store = f.store;
    REQUIRE(store.create("Default", "Recommended").ok());
    CHECK(store.create("default", "Recommended").error().code == ProfileError::NameTaken);
    CHECK(store.create("  DEFAULT ", "Recommended").error().code == ProfileError::NameTaken);
    CHECK(store.find("dEfAuLt") != nullptr);

    // Whitespace around a new name is trimmed off.
    const auto trimmed = store.create("  Alice\t", "Recommended");
    REQUIRE(trimmed.ok());
    CHECK((*trimmed)->name == "Alice");
}

TEST_CASE("names must be usable as file names") {
    Fixture f;
    ProfileStore& store = f.store;
    for (const char* name : {"   ", "\t"}) {
        CAPTURE(name);
        CHECK(store.create(name, "Recommended").error().code == ProfileError::EmptyName);
    }
    for (const char* name : {"a/b", "a\\b", "a:b", "a*", "why?", "\"quoted\"", "<tag>", "a|b", "bell\a",
                             "Alice.", "CON", "con", "Nul.toml", "com1", "LPT9", "aux.backup"}) {
        CAPTURE(name);
        const auto result = store.create(name, "Recommended");
        REQUIRE_FALSE(result.ok());
        CHECK(result.error().code == ProfileError::InvalidName);
    }
    // Close to reserved names but not reserved, and ordinary punctuation.
    for (const char* name : {"Console", "COM10", "LPT", "My profile (seated)", "v1.2", "Zo\xC3\xAB"}) {
        CAPTURE(name);
        CHECK(store.create(name, "Recommended").ok());
    }
}

TEST_CASE("the base preset must exist") {
    Fixture f;
    const auto result = f.store.create("Alice", "NoSuchPreset");
    REQUIRE_FALSE(result.ok());
    CHECK(result.error().code == ProfileError::UnknownPreset);
    CHECK(f.store.names().empty());
}

TEST_CASE("duplicate copies overrides under a new name") {
    Fixture f;
    ProfileStore& store = f.store;
    auto alice = store.create("Alice", "Recommended");
    REQUIRE(alice.ok());
    (*alice)->overrides["comfort.snap_turn_degrees"] = std::int64_t{60};

    const auto copy = store.duplicate("Alice", "Alice (copy)");
    REQUIRE(copy.ok());
    CHECK((*copy)->name == "Alice (copy)");
    CHECK((*copy)->basePreset == "Recommended");
    CHECK((*copy)->overrides.size() == 1);

    // The copy is independent of the original.
    (*copy)->overrides.clear();
    CHECK(store.find("Alice")->overrides.size() == 1);

    CHECK(store.duplicate("Nobody", "X").error().code == ProfileError::NotFound);
    CHECK(store.duplicate("Alice", "Alice (copy)").error().code == ProfileError::NameTaken);
    CHECK(store.duplicate("Alice", "ALICE").error().code == ProfileError::NameTaken);
    CHECK(store.duplicate("Alice", "Alice/2").error().code == ProfileError::InvalidName);
}

TEST_CASE("rename changes the name and keeps everything else") {
    Fixture f;
    ProfileStore& store = f.store;
    REQUIRE(store.create("Alice", "Recommended").ok());
    REQUIRE(store.create("Bob", "Recommended").ok());

    const auto renamed = store.rename("Alice", "Alicia");
    REQUIRE(renamed.ok());
    CHECK(store.find("Alice") == nullptr);
    REQUIRE(store.find("Alicia") != nullptr);
    CHECK(store.names() == std::vector<std::string>{"Alicia", "Bob"});

    CHECK(store.rename("Alicia", "Alicia").ok());
    CHECK(store.rename("Alicia", "Bob").error().code == ProfileError::NameTaken);
    CHECK(store.rename("Alicia", "bob").error().code == ProfileError::NameTaken);
    CHECK(store.rename("Alicia", "").error().code == ProfileError::EmptyName);
    CHECK(store.rename("Alicia", "PRN").error().code == ProfileError::InvalidName);
    CHECK(store.rename("Nobody", "X").error().code == ProfileError::NotFound);

    // Changing only the capitalisation of a profile's own name is allowed.
    REQUIRE(store.rename("alicia", "ALICIA").ok());
    CHECK(store.names() == std::vector<std::string>{"ALICIA", "Bob"});
}

TEST_CASE("remove deletes by name") {
    Fixture f;
    ProfileStore& store = f.store;
    REQUIRE(store.create("Alice", "Recommended").ok());
    CHECK(store.remove("alice"));
    CHECK_FALSE(store.remove("Alice"));
    CHECK(store.names().empty());
}
