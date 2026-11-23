#include "platform/mp_policy/profile_cvars.hpp"
#include "platform/mp_policy/saved_cvars.hpp"
#include "stereo_seq/seq_settings.hpp"
#include "ui_layer/crosshair.hpp"
#include "vkcore/parallel_eyes_settings.hpp"

#include <doctest/doctest.h>

#include <cstddef>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

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
    SavedCvars<int> book{};
    std::optional<std::string> loaded{};
    bool fixed = true; // the profile hooks are in
    std::string name = "view_skipShakes";

    std::optional<std::string> before() const {
        const auto* e = book.find(name);
        return e ? std::optional<std::string>(e->value) : std::nullopt;
    }
    void hold(const std::string& value) { // runtime_cvars::apply through the book
        if (live != value) {
            book.beforeWrite(name, live, 0);
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

TEST_CASE(
    "profile cvars: view_skipShakes, r_sharpening and the HUD scale, not the cvars the load sets again") {
    CHECK(profileStoredCvar("view_skipShakes"));
    CHECK(profileStoredCvar("VIEW_SKIPSHAKES"));
    CHECK(profileStoredCvar("r_sharpening"));
    CHECK(profileStoredCvar("swf_anchorProfile"));
    CHECK(evr::mp_policy::profileStoredCvars().size() == 3);
    // Photo mode is no longer held through its setting, so the hooks have nothing to keep for it.
    CHECK_FALSE(profileStoredCvar("g_setting_photomode"));
    CHECK_FALSE(profileStoredCvar("g_allowPhotoMode"));
    CHECK_FALSE(profileStoredCvar("r_antialiasing"));
    CHECK_FALSE(profileStoredCvar("r_raytracedReflectionsTemporalUpscaleQuality"));
    CHECK_FALSE(profileStoredCvar("view_skipKicks")); // not in the profile
    CHECK_FALSE(profileStoredCvar("view_skipShake"));
}

namespace {

// The first `|` field of each line of a launcher data file (forced-cvars.txt, cpu-saver.txt) that names a
// cvar: comments, blank lines and the CPU Saver's item and tip lines left out.
std::vector<std::string> dataFileCvars(const char* path) {
    std::vector<std::string> names;
    std::ifstream in(path);
    std::string line;
    while (std::getline(in, line)) {
        const std::size_t bar = line.find('|');
        if (line.empty() || line[0] == '#' || bar == std::string::npos) {
            continue;
        }
        std::string name = line.substr(0, bar);
        while (!name.empty() && (name.back() == ' ' || name.back() == '	')) {
            name.pop_back();
        }
        if (name != "item" && name != "tip" && !name.empty()) {
            names.push_back(name);
        }
    }
    return names;
}

} // namespace

TEST_CASE(
    "profile cvars: the game settings fields' cvars, none of them in a set the layer or launcher holds") {
    // The game copies these into the profile's GAME_FIELD_* fields before each save and back after each load,
    // outside the profile hooks: a held value there reached the flat game (photo mode, 0.1.99 to 0.1.101).
    using evr::mp_policy::profileFieldCvar;
    CHECK(evr::mp_policy::profileFieldCvars().size() == 16);
    CHECK(profileFieldCvar("g_setting_photomode"));
    CHECK(profileFieldCvar("G_SETTING_PHOTOMODE"));
    CHECK(profileFieldCvar("g_reticleMode"));
    CHECK(profileFieldCvar("g_settings_dashStyle"));
    CHECK_FALSE(profileFieldCvar("g_allowPhotoMode")); // nothing saves it
    for (const auto name : evr::mp_policy::profileFieldCvars()) {
        CHECK_FALSE(profileStoredCvar(name)); // the hooks' cvars are not among them
    }
    // Every held set a test can reach as data. Route S: its sets before play (TAA, DLSS at each quality), the
    // per-eye filters, SSDO, SSR, the window and present set, the comfort set and the HUD Scale; mono's set
    // and window set; the v1 sequential set; Parallel Eye Rendering's anti-aliasing (each mode); hand aim's
    // reticle hold; the profile hooks' own cvars (r_sharpening is the launcher's Sharpening); the launcher's
    // command line (forced-cvars.txt) and the CPU Saver's items (cpu-saver.txt).
    std::vector<std::string> held;
    const auto add = [&held](const auto& set) {
        for (const auto& c : set) {
            held.emplace_back(c.name);
        }
    };
    namespace seq = evr::stereo_seq;
    add(seq::stereoVrHeldCvars());
    add(seq::monoVrHeldCvars());
    add(seq::sequentialCvars());
    add(seq::stereoRuntimeCvars(seq::StereoTemporal::Off));
    add(seq::stereoRuntimeCvars(seq::StereoTemporal::PerEye));
    for (const int quality : {-1, 0, 1, 2, 3}) {
        add(seq::stereoCvarsBeforePlay(true, quality));
        for (const bool dlss : {false, true}) {
            add(evr::vkcore::parallel_eyes::antiAliasingCvars(false, dlss, quality));
            add(evr::vkcore::parallel_eyes::antiAliasingCvars(true, dlss, quality));
        }
    }
    add(seq::stereoCvarsBeforePlay(false, -1));
    for (const bool perEye : {false, true}) {
        add(std::vector<seq::CvarExpectation>{seq::stereoScatterFilterCvar(perEye),
                                              seq::stereoSsdoFilterCvar(perEye)});
    }
    add(std::vector<seq::CvarExpectation>{*seq::stereoSsdoCvar(""), *seq::stereoSsrCvar(""),
                                          seq::stereoHudScaleCvar()});
    add(seq::stereoWindowCvars("game.exe +r_windowWidth 1 +r_windowHeight 1"));
    add(seq::monoWindowCvars("game.exe +r_windowWidth 1 +r_windowHeight 1"));
    held.emplace_back(evr::ui_layer::kReticleStyleCvar);
    for (const auto name : evr::mp_policy::profileStoredCvars()) {
        held.emplace_back(name);
    }
    const std::vector<std::string> forced = dataFileCvars(EVR_FORCED_CVARS_FILE);
    const std::vector<std::string> saver = dataFileCvars(EVR_CPU_SAVER_FILE);
    REQUIRE(forced.size() > 20);
    REQUIRE(saver.size() > 5);
    held.insert(held.end(), forced.begin(), forced.end());
    held.insert(held.end(), saver.begin(), saver.end());
    for (const std::string& name : held) {
        INFO(name);
        CHECK_FALSE(profileFieldCvar(name));
    }
}

TEST_CASE("profile cvars: every comfort cvar mono's command line forces is held in head-tracked mono") {
    // The command line alone did not hold them (on the rig most never took): each `| mono` line of
    // forced-cvars.txt is held at run time at the same value, so the two lists stay in step.
    std::ifstream in(EVR_FORCED_CVARS_FILE);
    std::string line;
    int mono = 0;
    const auto trim = [](std::string text) {
        while (!text.empty() && (text.front() == ' ' || text.front() == '\t')) {
            text.erase(text.begin());
        }
        while (!text.empty() && (text.back() == ' ' || text.back() == '\t' || text.back() == '\r')) {
            text.pop_back();
        }
        return text;
    };
    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#') {
            continue;
        }
        std::vector<std::string> fields;
        std::size_t start = 0;
        for (std::size_t bar = line.find('|'); bar != std::string::npos; bar = line.find('|', start)) {
            fields.push_back(trim(line.substr(start, bar - start)));
            start = bar + 1;
        }
        fields.push_back(trim(line.substr(start)));
        if (fields.size() < 3 || fields[2] != "mono") {
            continue;
        }
        ++mono;
        INFO(fields[0]);
        bool held = false;
        for (const auto& c : evr::stereo_seq::monoVrHeldCvars()) {
            held = held || (c.name == fields[0] && sameCvarValue(c.value, fields[1]));
        }
        CHECK(held);
    }
    CHECK(mono >= 14);
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

// The HUD Scale (swf_anchorProfile: 0 Small, 1 Medium, 2 Large) held at Large in VR
// (stereo_seq::stereoHudScaleCvar). The game keeps it in profile.bin only, so without the hooks a VR session
// left the flat game at Large (rig: a GUI capture experiment holding 2 saved 2 into the rig's profile).
TEST_CASE("profile cvars: the HUD scale VR holds is one the profile hooks keep out of the profile") {
    const auto hud = evr::stereo_seq::stereoHudScaleCvar();
    CHECK(std::string(hud.name) == "swf_anchorProfile");
    CHECK(std::string(hud.value) == "2");
    CHECK(profileStoredCvar(hud.name));
}

TEST_CASE("profile cvars: positive control, without the hooks the held Large HUD reaches the profile") {
    Sim s{"1", std::string("1")};
    s.name = "swf_anchorProfile";
    s.fixed = false;
    s.load();
    s.hold("2");
    s.save();
    CHECK(s.file == std::optional<std::string>("2"));
}

TEST_CASE("profile cvars: the HUD scale's lifetime: held from the session's start, never in the profile") {
    SUBCASE("Route S: the profile loads before the first present; every save keeps Medium") {
        Sim s{"0", std::string("1")}; // registered at 0, the player's profile at Medium
        s.name = "swf_anchorProfile";
        s.load();
        CHECK(s.live == "1");
        s.hold("2"); // the first present
        CHECK(s.live == "2");
        s.save(); // the save about 20 s in
        CHECK(s.file == std::optional<std::string>("1"));
        CHECK(s.live == "2");
        s.hold("2");
        s.save(); // a later save
        CHECK(s.file == std::optional<std::string>("1"));
        CHECK(s.live == "2");
        // The session ends (a quit or a crash): nothing runs at the end, and the profile already holds the
        // player's Medium from every save; the flat game loads it.
        Sim flat{"0", s.file};
        flat.name = "swf_anchorProfile";
        flat.fixed = false;
        flat.load();
        CHECK(flat.live == "1");
    }
    SUBCASE("Parallel Eye Rendering: the hold writes before the profile's load") {
        Sim s{"0", std::string("0")}; // a player at Small
        s.name = "swf_anchorProfile";
        s.hold("2");
        s.load(); // seeded with 0 (before the layer's first write), then the profile's 0
        CHECK(s.loaded == std::optional<std::string>("0"));
        s.hold("2");
        s.save();
        CHECK(s.file == std::optional<std::string>("0"));
        CHECK(s.live == "2");
    }
    SUBCASE("a player already at Large: nothing to write, nothing changes") {
        Sim s{"0", std::string("2")};
        s.name = "swf_anchorProfile";
        s.load();
        s.hold("2");
        CHECK(s.book.find(s.name) == nullptr); // the hold found 2 and wrote nothing
        s.save();
        CHECK(s.file == std::optional<std::string>("2"));
    }
    SUBCASE("a trip gives the player's value back, and later saves leave it alone") {
        Sim s{"0", std::string("1")};
        s.name = "swf_anchorProfile";
        s.load();
        s.hold("2");
        s.book.takeAll();   // the multiplayer guard tripped: the layer stops holding
        s.live = *s.loaded; // cvar_book::setTripValue: the load's value, not the registered 0
        s.save();
        CHECK(s.file == std::optional<std::string>("1"));
        CHECK(s.live == "1");
    }
}

// The HUD Scale's hold yields to the player's choice in the game's menu (mp_policy::YieldingHold): the layer
// holds Large until the game shows a value it did not write while the hold stood confirmed.
namespace {

// The HUD Scale as the hold (runtime_cvars_yield.cpp) and the profile hooks (profile_hooks.cpp) see it.
struct HudSim {
    std::string live = "0"; // registered at 0
    std::optional<std::string> file = std::string("1");
    std::optional<std::string> loaded;
    std::optional<std::string> before; // the book: the value before the layer's first write
    evr::mp_policy::YieldingHold yield;

    void present() { // runtime_cvars::apply: yieldTurn, else the write
        const auto step = yield.step(live, "2");
        if (step == evr::mp_policy::YieldingHold::Step::Yield) {
            loaded = live; // profilePlayerChoice
        } else if (step == evr::mp_policy::YieldingHold::Step::Hold) {
            if (!before) {
                before = live;
            }
            live = "2";
        }
    }
    void load() { // beforeLoad, the game's load, afterLoad
        yield.profileLoaded();
        if (const auto seed = profileLoadSeed(live, before)) {
            live = *seed;
        }
        if (file) {
            live = *file;
        }
        loaded = live;
    }
    void save() { // beforeSave (playerChose, then profileWrite), the game's write, afterSave
        if (yield.step(live, "2") == evr::mp_policy::YieldingHold::Step::Yield) {
            loaded = live; // profilePlayerChoice
        }
        const std::string held = live;
        const auto w = profileWrite(live, loaded, before);
        file = w.swap ? w.value : live;
        live = held;
    }
};

} // namespace

TEST_CASE("profile cvars: a HUD Scale the player changes in the game's menu during VR sticks and is saved") {
    SUBCASE("seen at a present") {
        HudSim s;
        s.present(); // the first present, before the load
        s.load();
        s.present();
        s.present(); // the hold confirmed: Large live
        CHECK(s.live == "2");
        s.save();
        CHECK(s.file == std::optional<std::string>("1"));
        s.live = "0"; // the player picks Small in the game's menu
        s.present();
        CHECK(s.live == "0"); // not written back to Large
        CHECK(s.yield.yielded());
        s.save();
        CHECK(s.file == std::optional<std::string>("0"));
        s.present();
        s.save();
        CHECK(s.live == "0");
        CHECK(s.file == std::optional<std::string>("0"));
    }
    SUBCASE("seen at the save that follows the menu, before any present") {
        HudSim s;
        s.load();
        s.present();
        s.present();
        s.live = "0";
        s.save();
        CHECK(s.file == std::optional<std::string>("0"));
        s.present();
        CHECK(s.live == "0");
    }
    SUBCASE("the player picks Large: the same as the hold, and saved") {
        HudSim s;
        s.load();
        s.present();
        s.present();
        s.live = "2";
        s.present();
        s.save();
        CHECK(s.file == std::optional<std::string>("1")); // no change seen: the profile keeps theirs
    }
    SUBCASE("a profile load is not a choice: the hold is written again after it") {
        HudSim s;
        s.present();
        s.present(); // confirmed before the load (Parallel Eye Rendering's order)
        s.load();    // sets 1
        CHECK(s.live == "1");
        s.present();
        CHECK(s.live == "2");
        CHECK_FALSE(s.yield.yielded());
        s.save();
        CHECK(s.file == std::optional<std::string>("1"));
    }
}

TEST_CASE("profile cvars: a yielding hold writes until confirmed, then yields to a value it did not write") {
    using Step = evr::mp_policy::YieldingHold::Step;
    evr::mp_policy::YieldingHold y;
    CHECK(y.step("1", "2") == Step::Hold); // not confirmed yet: write
    CHECK(y.step("2", "2") == Step::Keep); // confirmed
    CHECK(y.step("2.0", "2") == Step::Keep);
    y.profileLoaded();
    CHECK(y.step("1", "2") == Step::Hold); // the load's value is not a choice
    CHECK(y.step("2", "2") == Step::Keep);
    CHECK(y.step("0", "2") == Step::Yield);
    CHECK(y.yielded());
    CHECK(y.step("0", "2") == Step::Keep);
    CHECK(y.step("1", "2") == Step::Keep); // never held again this session
    y.profileLoaded();
    CHECK(y.step("1", "2") == Step::Keep);
}
