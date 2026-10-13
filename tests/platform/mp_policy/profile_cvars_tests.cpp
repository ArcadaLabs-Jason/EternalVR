#include "platform/mp_policy/profile_cvars.hpp"
#include "platform/mp_policy/saved_cvars.hpp"

#include <doctest/doctest.h>

#include <optional>
#include <string>

using evr::mp_policy::profileLoadSeed;
using evr::mp_policy::profileStoredCvar;
using evr::mp_policy::profileWrite;
using evr::mp_policy::sameCvarValue;
using evr::mp_policy::SavedCvars;

namespace {

// One cvar as the game and the layer see it: the live value, the layer's book (its value before the layer's
// first write) and the profile file's entry (nothing: no key). load() and save() play the game's serializer
// (RVA 0x141D040): a load sets the cvar from the key when there is one, a save writes the live value.
struct Sim {
    std::string live;
    std::optional<std::string> file;
    SavedCvars<int> book;
    std::optional<std::string> loaded;
    bool fixed = true; // the profile hooks are in

    std::optional<std::string> before() const {
        const auto* e = book.find("view_skipShakes");
        return e ? std::optional<std::string>(e->value) : std::nullopt;
    }
    void hold(const std::string& value) { // runtime_cvars::apply through the book
        if (live != value) {
            book.beforeWrite("view_skipShakes", live, 0);
            live = value;
        }
    }
    void load() {
        if (fixed) {
            if (const auto seed = profileLoadSeed(live, before())) {
                live = *seed;
            }
        }
        if (file) {
            live = *file;
        }
        if (fixed) {
            loaded = live;
        }
    }
    void save() {
        const std::string held = live;
        const auto w = profileWrite(live, loaded, before());
        if (fixed && w.swap) {
            live = w.value;
        }
        file = live;
        live = held;
    }
};

} // namespace

TEST_CASE("profile cvars: view_skipShakes and r_sharpening, not the cvars the load sets again") {
    CHECK(profileStoredCvar("view_skipShakes"));
    CHECK(profileStoredCvar("VIEW_SKIPSHAKES"));
    CHECK(profileStoredCvar("r_sharpening"));
    CHECK_FALSE(profileStoredCvar("r_antialiasing"));
    CHECK_FALSE(profileStoredCvar("r_raytracedReflectionsTemporalUpscaleQuality"));
    CHECK_FALSE(profileStoredCvar("view_skipKicks")); // not in the profile
    CHECK_FALSE(profileStoredCvar("view_skipShake"));
}

TEST_CASE("profile cvars: values compare as numbers, else as text") {
    CHECK(sameCvarValue("1", "1"));
    CHECK(sameCvarValue("1", "1.0"));
    CHECK(sameCvarValue("2", "2.000000"));
    CHECK(sameCvarValue("1.99", "1.99"));
    CHECK_FALSE(sameCvarValue("1", "0"));
    CHECK_FALSE(sameCvarValue("1.99", "2"));
    CHECK(sameCvarValue("abc", "ABC"));
    CHECK_FALSE(sameCvarValue("1", "one"));
    CHECK_FALSE(sameCvarValue("", "0"));
}

TEST_CASE("profile cvars: a cvar the layer never wrote is saved as the game has it") {
    const auto w = profileWrite("1", std::string("0"), std::nullopt);
    CHECK_FALSE(w.swap);
    CHECK_FALSE(profileLoadSeed("1", std::nullopt).has_value());
}

TEST_CASE("profile cvars: a save gets the load's value, else the value before the layer's first write") {
    auto w = profileWrite("1", std::string("0"), std::string("0"));
    CHECK(w.swap);
    CHECK(w.value == "0");
    CHECK(std::string(w.source) == "the profile's load");

    // The load's value wins over the book's (the layer wrote before the load: the book holds the default).
    w = profileWrite("1", std::string("1"), std::string("0"));
    CHECK_FALSE(w.swap);
    CHECK(w.value == "1");

    // No load seen (the hook came too late): the value before the layer's first write.
    w = profileWrite("1", std::nullopt, std::string("0"));
    CHECK(w.swap);
    CHECK(w.value == "0");

    // r_sharpening held at 3 over the player's 1.99.
    w = profileWrite("3", std::string("1.99"), std::string("1.99"));
    CHECK(w.swap);
    CHECK(w.value == "1.99");
    CHECK_FALSE(profileWrite("2.0", std::string("2"), std::string("2")).swap);
}

TEST_CASE("profile cvars: a load first gets the value before the layer's first write") {
    CHECK(profileLoadSeed("1", std::string("0")) == std::optional<std::string>("0"));
    CHECK_FALSE(profileLoadSeed("0", std::string("0")).has_value());
}

TEST_CASE("profile cvars: positive control, without the hooks the held 1 reaches the profile") {
    // Stage 1 on the rig: profile 0, one stereo session, profile 1.
    Sim s{"0", std::string("0")};
    s.fixed = false;
    s.hold("1"); // Route S's first present, before the load
    s.load();
    CHECK(s.live == "0");
    s.hold("1");
    s.save();
    CHECK(s.file == std::optional<std::string>("1"));
}

TEST_CASE("profile cvars: with the hooks the profile keeps the player's value, the session keeps the hold") {
    SUBCASE("the first present before the load") {
        Sim s{"0", std::string("0")};
        s.hold("1");
        s.load();
        s.hold("1");
        s.save();
        CHECK(s.file == std::optional<std::string>("0"));
        CHECK(s.live == "1");
        s.save(); // every later save too
        CHECK(s.file == std::optional<std::string>("0"));
        CHECK(s.live == "1");
    }
    SUBCASE("the load before the first present") {
        Sim s{"0", std::string("0")};
        s.load();
        s.hold("1");
        s.save();
        CHECK(s.file == std::optional<std::string>("0"));
        CHECK(s.live == "1");
    }
    SUBCASE("a player who chose 1 keeps 1") {
        Sim s{"0", std::string("1")};
        s.hold("1");
        s.load();
        s.hold("1");
        s.save();
        CHECK(s.file == std::optional<std::string>("1"));
    }
    SUBCASE("a profile without the key: the game's own value, not the held one") {
        Sim s{"0", std::nullopt};
        s.hold("1"); // the layer wrote before the load
        s.load();    // no key: the seed (0) stays
        CHECK(s.loaded == std::optional<std::string>("0"));
        s.hold("1");
        s.save();
        CHECK(s.file == std::optional<std::string>("0"));
        CHECK(s.live == "1");
    }
    SUBCASE("a profile that already holds the leaked 1 keeps it (no repair)") {
        Sim s{"0", std::string("1")};
        s.hold("1");
        s.load();
        s.save();
        CHECK(s.file == std::optional<std::string>("1"));
    }
}

TEST_CASE("profile cvars: after a trip the book is empty and the save is left alone") {
    SavedCvars<int> book;
    book.beforeWrite("view_skipShakes", "0", 0);
    book.takeAll(); // the multiplayer guard tripped: the player's values went back
    const auto* e = book.find("view_skipShakes");
    const auto w =
        profileWrite("0", std::string("0"), e ? std::optional<std::string>(e->value) : std::nullopt);
    CHECK_FALSE(w.swap);
}

TEST_CASE("profile cvars: two loads (a profile switch) each give the player's value") {
    Sim s{"0", std::string("0")};
    s.hold("1");
    s.load();
    s.hold("1");
    s.save();
    CHECK(s.file == std::optional<std::string>("0"));
    // Another profile, whose player chose 1: the second load seeds the pre-layer 0 again, then reads 1.
    s.file = std::string("1");
    s.load();
    CHECK(s.loaded == std::optional<std::string>("1"));
    s.hold("1");
    s.save();
    CHECK(s.file == std::optional<std::string>("1"));
    // And back to a profile without the key: the game's own 0, not the last profile's 1.
    s.file = std::nullopt;
    s.load();
    CHECK(s.loaded == std::optional<std::string>("0"));
    s.hold("1");
    s.save();
    CHECK(s.file == std::optional<std::string>("0"));
}

TEST_CASE("profile cvars: the one-time repair only when asked and the profile holds 1") {
    using evr::mp_policy::ProfileRepair;
    using evr::mp_policy::profileRepair;
    using evr::mp_policy::profileRepairStatus;
    CHECK(profileRepair(false, std::string("1")) == ProfileRepair::Off);
    CHECK(profileRepair(true, std::string("1")) == ProfileRepair::Repair);
    CHECK(profileRepair(true, std::string("1.0")) == ProfileRepair::Repair);
    CHECK(profileRepair(true, std::string("0")) == ProfileRepair::NotNeeded);
    CHECK(profileRepair(true, std::nullopt) == ProfileRepair::NotNeeded);

    CHECK(std::string(profileRepairStatus(ProfileRepair::Off, false)) == "off");
    CHECK(std::string(profileRepairStatus(ProfileRepair::NotNeeded, false)) == "not_needed");
    // "done" only once a save wrote it: a crash before the save leaves the launcher's marker unset.
    CHECK(profileRepairStatus(ProfileRepair::Repair, false) == nullptr);
    CHECK(std::string(profileRepairStatus(ProfileRepair::Repair, true)) == "done");
}

TEST_CASE("profile cvars: a repaired profile saves 0 while the session holds 1") {
    Sim s{"0", std::string("1")}; // left by a session before the hooks
    s.hold("1");
    s.load();
    REQUIRE(evr::mp_policy::profileRepair(true, s.loaded) == evr::mp_policy::ProfileRepair::Repair);
    s.live = "0"; // the repair
    s.loaded = s.live;
    s.hold("1");
    s.save();
    CHECK(s.file == std::optional<std::string>("0"));
    CHECK(s.live == "1");
}
