#include "platform/settings/builtin.hpp"

#include <doctest/doctest.h>

#include <initializer_list>
#include <ostream>
#include <string>
#include <vector>

using evr::settings::ValidationStatus;

TEST_CASE("every built-in definition and preset registers") {
    // In release builds nothing else would notice a refused built-in, so this test is the check.
    const auto builtins = evr::settings::makeBuiltinSettings();
    for (const std::string& error : builtins.errors) {
        FAIL_CHECK(error);
    }
    CHECK(builtins.errors.empty());
}

TEST_CASE("built-in schema contains the initial keys") {
    const auto builtins = evr::settings::makeBuiltinSettings();
    for (const char* key : {evr::settings::keys::kVignetteStrength, evr::settings::keys::kTurnMode,
                            evr::settings::keys::kSnapTurnDegrees, evr::settings::keys::kCinematicFades,
                            evr::settings::keys::kFoveationPreset, evr::settings::keys::kPostureMode,
                            evr::settings::keys::kRuntimeJson}) {
        CAPTURE(key);
        const auto* def = builtins.schema.find(key);
        REQUIRE(def != nullptr);
        CHECK_FALSE(def->description.empty());
    }
}

TEST_CASE("built-in presets are registered in launcher order") {
    const auto builtins = evr::settings::makeBuiltinSettings();
    CHECK(builtins.presets.names() ==
          std::vector<std::string>{"Comfortable", "Recommended", "Advanced", "Intense"});
}

TEST_CASE("every built-in preset value validates cleanly against the schema") {
    const auto builtins = evr::settings::makeBuiltinSettings();
    for (const std::string& name : builtins.presets.names()) {
        for (const auto& entry : builtins.presets.find(name)->values) {
            const std::string& key = entry.first;
            CAPTURE(name);
            CAPTURE(key);
            CHECK(builtins.schema.validate(key, entry.second).status == ValidationStatus::Ok);
        }
    }
}
