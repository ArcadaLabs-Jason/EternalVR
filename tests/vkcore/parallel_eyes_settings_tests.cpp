#include "vkcore/parallel_eyes_settings.hpp"

#include <doctest/doctest.h>

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace pe = evr::vkcore::parallel_eyes;

namespace {

pe::Settings read(const std::map<std::wstring, std::wstring>& vars, bool uiLayer = true) {
    return pe::readSettings(
        [&vars](std::wstring_view name) -> std::optional<std::wstring> {
            const auto it = vars.find(std::wstring(name));
            if (it == vars.end()) {
                return std::nullopt;
            }
            return it->second;
        },
        [uiLayer] { return uiLayer; });
}

const std::map<std::wstring, std::wstring> kOn = {{L"ETERNALVR_PARALLEL_EYES", L"1"},
                                                  {L"ETERNALVR_MODE", L"stereo"}};

std::map<std::wstring, std::wstring> onWith(const std::wstring& name, const std::wstring& value) {
    auto vars = kOn;
    vars[name] = value;
    return vars;
}

} // namespace

TEST_CASE("off unless ETERNALVR_PARALLEL_EYES is 1, with nothing to say") {
    for (const auto& vars :
         {std::map<std::wstring, std::wstring>{},
          std::map<std::wstring, std::wstring>{{L"ETERNALVR_MODE", L"stereo"}},
          std::map<std::wstring, std::wstring>{{L"ETERNALVR_PARALLEL_EYES", L"0"},
                                               {L"ETERNALVR_MODE", L"stereo"}},
          std::map<std::wstring, std::wstring>{{L"ETERNALVR_PARALLEL_EYES", L""},
                                               {L"ETERNALVR_MODE", L"stereo"}},
          std::map<std::wstring, std::wstring>{{L"ETERNALVR_PARALLEL_EYES", L"yes"},
                                               {L"ETERNALVR_MODE", L"stereo"},
                                               {L"ETERNALVR_TEST_VIEW_OFF", L"nonsense"}}}) {
        const pe::Settings s = read(vars);
        CHECK_FALSE(s.requested);
        CHECK(s.why.empty());
        CHECK(s.warnings.empty());
    }
}

TEST_CASE("on in stereo selects the clones and the screen-pass eye copy, nothing off") {
    const pe::Settings s = read(kOn);
    CHECK(s.requested);
    CHECK(s.why.empty());
    CHECK(s.clones);
    CHECK(s.eyeCopy == pe::EyeCopy::Screen);
    CHECK(s.off == 0);
    CHECK(s.viewOnly == -1);
    CHECK(s.warnings.empty());
    CHECK(read({{L"ETERNALVR_PARALLEL_EYES", L" 1 "}, {L"ETERNALVR_MODE", L"STEREO"}}).requested);
}

TEST_CASE("ruled out by mono, a stereo experiment or the UI layer off, with the reason") {
    const pe::Settings mono = read({{L"ETERNALVR_PARALLEL_EYES", L"1"}});
    CHECK_FALSE(mono.requested);
    CHECK(mono.why.find("stereo only") != std::string::npos);
    CHECK_FALSE(read({{L"ETERNALVR_PARALLEL_EYES", L"1"}, {L"ETERNALVR_MODE", L"mono"}}).requested);
    const pe::Settings experiment = read(onWith(L"ETERNALVR_STEREO_EXPERIMENT", L"two-views"));
    CHECK_FALSE(experiment.requested);
    CHECK(experiment.why.find("ETERNALVR_STEREO_EXPERIMENT") != std::string::npos);
    CHECK(read(onWith(L"ETERNALVR_STEREO_EXPERIMENT", L"")).requested);
    const pe::Settings ui = read(kOn, false);
    CHECK_FALSE(ui.requested);
    CHECK(ui.why.find("UI layer") != std::string::npos);
}

TEST_CASE("alternate eyes, a mode of Route S, is noted to be ignored") {
    CHECK_FALSE(read(kOn).alternateEyes);
    CHECK_FALSE(read(onWith(L"ETERNALVR_ALTERNATE_EYES", L"0")).alternateEyes);
    CHECK(read(onWith(L"ETERNALVR_ALTERNATE_EYES", L"1")).alternateEyes);
    const pe::Settings automatic = read(onWith(L"ETERNALVR_ALTERNATE_EYES", L"auto"));
    CHECK(automatic.alternateEyes);
    CHECK(automatic.requested); // it does not keep Parallel Eye Rendering off
}

TEST_CASE("with DLSS: DLSS in both views") {
    for (const wchar_t* on : {L"1", L"true", L"ON"}) {
        const pe::Settings s = read(onWith(L"ETERNALVR_STEREO_DLSS", on));
        CHECK(s.requested);
        CHECK(s.dlss);
        CHECK(s.warnings.empty());
    }
    CHECK_FALSE(read(kOn).dlss);
    CHECK_FALSE(read(onWith(L"ETERNALVR_STEREO_DLSS", L"0")).dlss);
    CHECK(read(onWith(L"ETERNALVR_STEREO_DLSS", L"")).requested);
    auto knob = onWith(L"ETERNALVR_STEREO_DLSS", L"1");
    knob[L"ETERNALVR_PE_DLSS"] = L"1";
    CHECK(read(knob).dlss);
    CHECK(read(knob).warnings.empty());
    // The launcher's Off wins: no anti-aliasing, as Route S without per-eye TAA.
    auto off = onWith(L"ETERNALVR_STEREO_DLSS", L"1");
    off[L"ETERNALVR_STEREO_TAA"] = L"0";
    CHECK(read(off).requested);
    CHECK_FALSE(read(off).dlss);
}

TEST_CASE("ETERNALVR_PE_DLSS=0 keeps the standard renderer for DLSS, which runs it per eye") {
    auto vars = onWith(L"ETERNALVR_STEREO_DLSS", L"1");
    vars[L"ETERNALVR_PE_DLSS"] = L" 0 ";
    const pe::Settings s = read(vars);
    CHECK_FALSE(s.requested);
    CHECK_FALSE(s.dlss);
    CHECK(s.why.find("not with DLSS") != std::string::npos);
    CHECK(s.why.find("ETERNALVR_PE_DLSS=0") != std::string::npos);
    // Without DLSS it changes nothing.
    CHECK(read(onWith(L"ETERNALVR_PE_DLSS", L"0")).requested);
    // Another value: noted, DLSS in both views.
    vars[L"ETERNALVR_PE_DLSS"] = L"maybe";
    const pe::Settings other = read(vars);
    CHECK(other.requested);
    CHECK(other.dlss);
    REQUIRE(other.warnings.size() == 1);
    CHECK(other.warnings[0].find("ETERNALVR_PE_DLSS") != std::string::npos);
}

TEST_CASE("the anti-aliasing is TAA, or none with ETERNALVR_STEREO_TAA=0, held as the stereo path holds it") {
    CHECK_FALSE(read(kOn).antiAliasingOff);
    CHECK_FALSE(read(onWith(L"ETERNALVR_STEREO_TAA", L"1")).antiAliasingOff);
    CHECK(read(onWith(L"ETERNALVR_STEREO_TAA", L"0")).antiAliasingOff);
    CHECK(read(onWith(L"ETERNALVR_STEREO_TAA", L"off")).antiAliasingOff);
    const auto value = [](const std::vector<evr::stereo_seq::CvarExpectation>& cvars, std::string_view name) {
        for (const auto& c : cvars) {
            if (c.name == name) {
                return std::string(c.value);
            }
        }
        return std::string("unset");
    };
    const auto taa = pe::antiAliasingCvars(false);
    CHECK(taa.size() == 1);
    CHECK(value(taa, "r_antialiasing") == "1");
    const auto off = pe::antiAliasingCvars(true);
    CHECK(value(off, "r_antialiasing") == "0");
    CHECK(value(off, "r_TAASafeMode") == "1");
    CHECK(value(pe::antiAliasingCvars(true, true, 3), "r_antialiasing") == "0"); // Off wins
    const auto dlss = pe::antiAliasingCvars(false, true, 2);
    CHECK(dlss.size() == 2);
    CHECK(value(dlss, "r_antialiasing") == "2");
    CHECK(value(dlss, "r_dlssQuality") == "2");
    const auto own = pe::antiAliasingCvars(false, true, -1); // the player's own quality
    CHECK(own.size() == 1);
    CHECK(value(own, "r_antialiasing") == "2");
    CHECK(value(pe::antiAliasingCvars(false, true, 7), "r_dlssQuality") == "unset");
}

TEST_CASE("ETERNALVR_STEREO_RUNTIME_CVARS=0 leaves the anti-aliasing as the game has it, as under Route S") {
    CHECK(read(kOn).antiAliasingHeld);
    CHECK(read(onWith(L"ETERNALVR_STEREO_RUNTIME_CVARS", L"1")).antiAliasingHeld);
    const pe::Settings off = read(onWith(L"ETERNALVR_STEREO_RUNTIME_CVARS", L"0"));
    CHECK_FALSE(off.antiAliasingHeld);
    CHECK(off.requested); // the rest of Parallel Eye Rendering runs
    CHECK(off.warnings.empty());
    // Read as runtime_cvars.cpp reads it: exactly "0".
    CHECK(read(onWith(L"ETERNALVR_STEREO_RUNTIME_CVARS", L" 0")).antiAliasingHeld);
    auto both = onWith(L"ETERNALVR_STEREO_RUNTIME_CVARS", L"0");
    both[L"ETERNALVR_STEREO_TAA"] = L"0";
    CHECK_FALSE(read(both).antiAliasingHeld); // with the launcher's Off too
    // With DLSS nothing would hold TAA after a fallback: the standard renderer runs DLSS per eye.
    auto dlss = onWith(L"ETERNALVR_STEREO_RUNTIME_CVARS", L"0");
    dlss[L"ETERNALVR_STEREO_DLSS"] = L"1";
    const pe::Settings refused = read(dlss);
    CHECK_FALSE(refused.requested);
    CHECK_FALSE(refused.dlss);
    CHECK(refused.why.find("ETERNALVR_STEREO_RUNTIME_CVARS=0") != std::string::npos);
}

TEST_CASE("the mode and the experiment are read untrimmed, as the rest of the layer reads them") {
    // " stereo" is not stereo to the presenter (it runs mono), so it is not stereo here.
    const pe::Settings mode = read({{L"ETERNALVR_PARALLEL_EYES", L"1"}, {L"ETERNALVR_MODE", L" stereo"}});
    CHECK_FALSE(mode.requested);
    CHECK(mode.why.find("stereo only") != std::string::npos);
    // " " is a set experiment to the rest of the layer (Route S's modules stay off), so it rules this out.
    const pe::Settings experiment = read(onWith(L"ETERNALVR_STEREO_EXPERIMENT", L" "));
    CHECK_FALSE(experiment.requested);
    CHECK(experiment.why.find("ETERNALVR_STEREO_EXPERIMENT") != std::string::npos);
}

TEST_CASE("the test knobs change what it selects, for experiments") {
    CHECK_FALSE(read(onWith(L"ETERNALVR_TEST_VIEW_CLONES", L"0")).clones);
    CHECK(read(onWith(L"ETERNALVR_TEST_VIEW_CLONES", L"1")).clones);
    CHECK(read(onWith(L"ETERNALVR_TEST_EYE_COPY", L"1")).eyeCopy == pe::EyeCopy::Final);
    // The default has no name of its own: anything but 0 or 1 is a warning.
    const pe::Settings ldr = read(onWith(L"ETERNALVR_TEST_EYE_COPY", L"ldr"));
    CHECK(ldr.eyeCopy == pe::EyeCopy::Screen);
    CHECK(ldr.warnings.size() == 1);
    CHECK(read(onWith(L"ETERNALVR_TEST_EYE_COPY", L"0")).eyeCopy == pe::EyeCopy::Off);
    CHECK_FALSE(
        read(onWith(L"ETERNALVR_TEST_VIEW_ONLY", L"1")).latestPose); // default: the shown frame's pose
    CHECK(read(onWith(L"ETERNALVR_TEST_PE_POSE", L"latest")).latestPose);
    CHECK(read(onWith(L"ETERNALVR_TEST_PE_POSE", L"newest")).warnings.size() == 1);
    CHECK_FALSE(read(onWith(L"ETERNALVR_TEST_VIEW_ONLY", L"1")).showRepeats); // default: the last pair stays
    CHECK(read(onWith(L"ETERNALVR_TEST_PE_REPEATS", L"show")).showRepeats);
    CHECK(read(onWith(L"ETERNALVR_TEST_PE_REPEATS", L"1")).warnings.size() == 1);
    CHECK(read(onWith(L"ETERNALVR_TEST_VIEW_ONLY", L"1")).viewOnly == 1);
    // Without the clones eye 1 has no image of its own.
    auto noClones = onWith(L"ETERNALVR_TEST_VIEW_CLONES", L"0");
    noClones[L"ETERNALVR_TEST_EYE_COPY"] = L"1";
    CHECK(read(noClones).eyeCopy == pe::EyeCopy::Off);
}

TEST_CASE("ETERNALVR_TEST_VIEW_CLONE_LOG=0 turns the clone census off") {
    CHECK(read(kOn).cloneCensus);
    CHECK(read(onWith(L"ETERNALVR_TEST_VIEW_CLONE_LOG", L"1")).cloneCensus);
    const pe::Settings off = read(onWith(L"ETERNALVR_TEST_VIEW_CLONE_LOG", L" 0"));
    CHECK_FALSE(off.cloneCensus);
    CHECK(off.warnings.empty());
    const pe::Settings bad = read(onWith(L"ETERNALVR_TEST_VIEW_CLONE_LOG", L"census"));
    CHECK(bad.cloneCensus);
    REQUIRE(bad.warnings.size() == 1);
    CHECK(bad.warnings[0].find("ETERNALVR_TEST_VIEW_CLONE_LOG") != std::string::npos);
}

TEST_CASE("ETERNALVR_TEST_VIEW_CLONE_SKIP: slot0 by default; set, exactly the items it lists") {
    const pe::CloneSkip byDefault = read(kOn).cloneSkip;
    CHECK(byDefault.groups == pe::kSkipSlot0);
    CHECK(byDefault.slots.empty());
    CHECK(byDefault.fields.empty());
    CHECK(pe::cloneSkipText(byDefault) == "slot0");
    CHECK(read(onWith(L"ETERNALVR_TEST_VIEW_CLONE_SKIP", L" ")).cloneSkip.groups == pe::kSkipSlot0);
    const pe::Settings none = read(onWith(L"ETERNALVR_TEST_VIEW_CLONE_SKIP", L"none"));
    CHECK(none.cloneSkip.groups == 0);
    CHECK(none.warnings.empty());
    CHECK(pe::cloneSkipText(none.cloneSkip) == "none");
    // dof alone leaves slot 0's clones in: the list replaces the default.
    CHECK(read(onWith(L"ETERNALVR_TEST_VIEW_CLONE_SKIP", L"dof")).cloneSkip.groups == pe::kSkipDof);
    const pe::Settings all =
        read(onWith(L"ETERNALVR_TEST_VIEW_CLONE_SKIP",
                    L"Slot0, dof,GUI,flares ,mblur,Refract,0x66E3180,66e3378,dc+0x5E0,DC+2a0"));
    CHECK(all.warnings.empty());
    CHECK(all.cloneSkip.groups == (pe::kSkipSlot0 | pe::kSkipDof | pe::kSkipGui | pe::kSkipFlares |
                                   pe::kSkipMotionBlur | pe::kSkipRefraction));
    CHECK(all.cloneSkip.slots == std::vector<std::uint32_t>{0x66E3180, 0x66E3378});
    CHECK(all.cloneSkip.fields == std::vector<std::uint32_t>{0x5E0, 0x2A0});
    CHECK(pe::cloneSkipText(all.cloneSkip) ==
          "slot0,dof,gui,flares,mblur,refract,0x66E3180,0x66E3378,dc+0x5E0,dc+0x2A0");
    // The refraction clones (glass per view) are made by default; refract leaves them shared as well.
    CHECK((byDefault.groups & pe::kSkipRefraction) == 0);
    const pe::Settings refract = read(onWith(L"ETERNALVR_TEST_VIEW_CLONE_SKIP", L"slot0,refract"));
    CHECK(refract.warnings.empty());
    CHECK(refract.cloneSkip.groups == (pe::kSkipSlot0 | pe::kSkipRefraction));
    CHECK(pe::cloneSkipText(refract.cloneSkip) == "slot0,refract");
    // View 1's binds on the transparency pass's own block take its clones by default; tblock leaves them.
    CHECK((byDefault.groups & pe::kSkipTransparencyBlock) == 0);
    const pe::Settings tblock = read(onWith(L"ETERNALVR_TEST_VIEW_CLONE_SKIP", L"slot0,TBlock"));
    CHECK(tblock.warnings.empty());
    CHECK(tblock.cloneSkip.groups == (pe::kSkipSlot0 | pe::kSkipTransparencyBlock));
    CHECK(pe::cloneSkipText(tblock.cloneSkip) == "slot0,tblock");
}

TEST_CASE("ETERNALVR_TEST_VIEW_CLONE_SKIP: an item that does not parse is a warning") {
    std::vector<std::string> warnings;
    // Not a multiple of 8, a slot below 0x1000 (an offset without dc+), an offset of 0x1000, not hex, too
    // long.
    const pe::CloneSkip skip =
        pe::readCloneSkip(L"dof,0x66E3181,0x5E0,dc+0x1000,dc+x,dc+,slot1,0x123456789", warnings);
    CHECK(skip.groups == pe::kSkipDof);
    CHECK(skip.slots.empty());
    CHECK(skip.fields.empty());
    CHECK(warnings.size() == 7);
    CHECK(warnings[0].find("'0x66E3181'") != std::string::npos);
    // Nothing usable: the default, with one more warning.
    warnings.clear();
    const pe::CloneSkip typo = pe::readCloneSkip(L"slot 0", warnings);
    CHECK(typo.groups == pe::kSkipSlot0);
    CHECK(warnings.size() == 2);
    CHECK(warnings[1].find("slot0 as by default") != std::string::npos);
}

TEST_CASE("ETERNALVR_TEST_VIEW_CLONE_NAMES=build and ETERNALVR_TEST_VIEW_CLONE_REBUILD, for the rig") {
    const pe::Settings byDefault = read(kOn);
    CHECK_FALSE(byDefault.cloneBuildNames);
    CHECK(byDefault.cloneRebuildSeconds == 0);
    CHECK(read(onWith(L"ETERNALVR_TEST_VIEW_CLONE_NAMES", L"Build ")).cloneBuildNames);
    const pe::Settings names = read(onWith(L"ETERNALVR_TEST_VIEW_CLONE_NAMES", L"stable"));
    CHECK_FALSE(names.cloneBuildNames);
    CHECK(names.warnings.size() == 1);
    CHECK(read(onWith(L"ETERNALVR_TEST_VIEW_CLONE_REBUILD", L" 30")).cloneRebuildSeconds == 30);
    CHECK(read(onWith(L"ETERNALVR_TEST_VIEW_CLONE_REBUILD", L"3600")).cloneRebuildSeconds == 3600);
    for (const wchar_t* bad : {L"0", L"3601", L"-5", L"1.5", L"30s", L"9999999999"}) {
        const pe::Settings s = read(onWith(L"ETERNALVR_TEST_VIEW_CLONE_REBUILD", bad));
        CHECK(s.cloneRebuildSeconds == 0);
        REQUIRE(s.warnings.size() == 1);
        CHECK(s.warnings[0].find("ETERNALVR_TEST_VIEW_CLONE_REBUILD") != std::string::npos);
    }
}

TEST_CASE("ETERNALVR_TEST_PE_UPDATE_UNION=0 leaves view 1's update lists out") {
    CHECK(read(kOn).updateUnion);
    CHECK(read(onWith(L"ETERNALVR_TEST_PE_UPDATE_UNION", L"1")).updateUnion);
    const pe::Settings off = read(onWith(L"ETERNALVR_TEST_PE_UPDATE_UNION", L" 0 "));
    CHECK_FALSE(off.updateUnion);
    CHECK(off.requested);
    CHECK(off.warnings.empty());
    const pe::Settings bad = read(onWith(L"ETERNALVR_TEST_PE_UPDATE_UNION", L"off"));
    CHECK(bad.updateUnion);
    REQUIRE(bad.warnings.size() == 1);
    CHECK(bad.warnings[0].find("ETERNALVR_TEST_PE_UPDATE_UNION") != std::string::npos);
}

TEST_CASE("ETERNALVR_TEST_PE_SWAP_GUARD=0 leaves view 0's screen pass in after a swapchain recreate") {
    CHECK(read(kOn).swapGuard);
    CHECK(read(onWith(L"ETERNALVR_TEST_PE_SWAP_GUARD", L"1")).swapGuard);
    const pe::Settings off = read(onWith(L"ETERNALVR_TEST_PE_SWAP_GUARD", L" 0"));
    CHECK_FALSE(off.swapGuard);
    CHECK(off.requested);
    CHECK(off.warnings.empty());
    const pe::Settings bad = read(onWith(L"ETERNALVR_TEST_PE_SWAP_GUARD", L"no"));
    CHECK(bad.swapGuard);
    REQUIRE(bad.warnings.size() == 1);
    CHECK(bad.warnings[0].find("ETERNALVR_TEST_PE_SWAP_GUARD") != std::string::npos);
}

TEST_CASE("each eye from its frame's own copies, two pairs kept; ETERNALVR_TEST_PE_PAIRING=guess as before") {
    const pe::Settings on = read(kOn);
    CHECK_FALSE(on.guessPairs);
    CHECK(on.pairSlots == 2);
    CHECK(read(onWith(L"ETERNALVR_TEST_PE_PAIRING", L" Guess ")).guessPairs);
    const pe::Settings frame = read(onWith(L"ETERNALVR_TEST_PE_PAIRING", L"frame"));
    CHECK_FALSE(frame.guessPairs);
    CHECK(frame.warnings.empty());
    const pe::Settings bad = read(onWith(L"ETERNALVR_TEST_PE_PAIRING", L"1"));
    CHECK_FALSE(bad.guessPairs);
    REQUIRE(bad.warnings.size() == 1);
    CHECK(bad.warnings[0].find("ETERNALVR_TEST_PE_PAIRING") != std::string::npos);
    CHECK(read(onWith(L"ETERNALVR_TEST_PE_PAIR_SLOTS", L"3")).pairSlots == 3);
    CHECK(read(onWith(L"ETERNALVR_TEST_PE_PAIR_SLOTS", L"4")).pairSlots == 4);
    for (const wchar_t* odd : {L"1", L"5", L"34", L"x"}) {
        const pe::Settings s = read(onWith(L"ETERNALVR_TEST_PE_PAIR_SLOTS", odd));
        CHECK(s.pairSlots == 2);
        CHECK(s.warnings.size() == 1);
    }
}

TEST_CASE("ETERNALVR_TEST_PE_EYE1_LAG=1 shows eye 1 a frame late and keeps at least three pairs") {
    const pe::Settings on = read(kOn);
    CHECK_FALSE(on.eye1Lag);
    const pe::Settings lag = read(onWith(L"ETERNALVR_TEST_PE_EYE1_LAG", L" 1"));
    CHECK(lag.eye1Lag);
    CHECK(lag.requested);
    CHECK(lag.pairSlots == 3);
    CHECK(lag.warnings.empty());
    auto four = onWith(L"ETERNALVR_TEST_PE_EYE1_LAG", L"1");
    four[L"ETERNALVR_TEST_PE_PAIR_SLOTS"] = L"4";
    CHECK(read(four).pairSlots == 4);
    const pe::Settings zero = read(onWith(L"ETERNALVR_TEST_PE_EYE1_LAG", L"0"));
    CHECK_FALSE(zero.eye1Lag);
    CHECK(zero.pairSlots == 2);
    CHECK(zero.warnings.empty());
    const pe::Settings bad = read(onWith(L"ETERNALVR_TEST_PE_EYE1_LAG", L"2"));
    CHECK_FALSE(bad.eye1Lag);
    CHECK(bad.pairSlots == 2);
    REQUIRE(bad.warnings.size() == 1);
    CHECK(bad.warnings[0].find("ETERNALVR_TEST_PE_EYE1_LAG") != std::string::npos);
    auto guess = onWith(L"ETERNALVR_TEST_PE_EYE1_LAG", L"1");
    guess[L"ETERNALVR_TEST_PE_PAIRING"] = L"guess";
    const pe::Settings withGuess = read(guess);
    CHECK_FALSE(withGuess.eye1Lag);
    CHECK(withGuess.pairSlots == 2);
    REQUIRE(withGuess.warnings.size() == 1);
    CHECK(withGuess.warnings[0].find("ETERNALVR_TEST_PE_PAIRING=guess") != std::string::npos);
}

TEST_CASE("ETERNALVR_TEST_PE_DROP=<N> leaves out every Nth present with a new pair, 2 to 60") {
    CHECK(read(kOn).dropEvery == 0);
    const pe::Settings two = read(onWith(L"ETERNALVR_TEST_PE_DROP", L"2"));
    CHECK(two.dropEvery == 2);
    CHECK(two.requested);
    CHECK(two.warnings.empty());
    CHECK(read(onWith(L"ETERNALVR_TEST_PE_DROP", L" 60 ")).dropEvery == 60);
    const pe::Settings zero = read(onWith(L"ETERNALVR_TEST_PE_DROP", L"0"));
    CHECK(zero.dropEvery == 0);
    CHECK(zero.warnings.empty());
    for (const wchar_t* odd : {L"1", L"61", L"-3", L"5x", L"", L"on"}) {
        const pe::Settings s = read(onWith(L"ETERNALVR_TEST_PE_DROP", odd));
        CHECK(s.dropEvery == 0);
        REQUIRE(s.warnings.size() == 1);
        CHECK(s.warnings[0].find("ETERNALVR_TEST_PE_DROP") != std::string::npos);
    }
    auto guess = onWith(L"ETERNALVR_TEST_PE_DROP", L"5");
    guess[L"ETERNALVR_TEST_PE_PAIRING"] = L"guess";
    const pe::Settings withGuess = read(guess);
    CHECK(withGuess.dropEvery == 0);
    REQUIRE(withGuess.warnings.size() == 1);
    CHECK(withGuess.warnings[0].find("ETERNALVR_TEST_PE_PAIRING=guess") != std::string::npos);
}

TEST_CASE("ETERNALVR_TEST_PE_RT_UPSCALE_HOLD=0 leaves the reflections' upscale quality to the game") {
    CHECK(read(kOn).rtUpscaleHold);
    CHECK(read(onWith(L"ETERNALVR_TEST_PE_RT_UPSCALE_HOLD", L"1")).rtUpscaleHold);
    const pe::Settings off = read(onWith(L"ETERNALVR_TEST_PE_RT_UPSCALE_HOLD", L"0 "));
    CHECK_FALSE(off.rtUpscaleHold);
    CHECK(off.requested);
    CHECK(off.warnings.empty());
    const pe::Settings bad = read(onWith(L"ETERNALVR_TEST_PE_RT_UPSCALE_HOLD", L"game"));
    CHECK(bad.rtUpscaleHold);
    REQUIRE(bad.warnings.size() == 1);
    CHECK(bad.warnings[0].find("ETERNALVR_TEST_PE_RT_UPSCALE_HOLD") != std::string::npos);
}

TEST_CASE("ETERNALVR_TEST_PE_WATER=0 leaves view 1's water on the world's state") {
    CHECK(read(kOn).water);
    CHECK(read(onWith(L"ETERNALVR_TEST_PE_WATER", L"1")).water);
    const pe::Settings off = read(onWith(L"ETERNALVR_TEST_PE_WATER", L" 0"));
    CHECK_FALSE(off.water);
    CHECK(off.requested);
    CHECK(off.warnings.empty());
    const pe::Settings bad = read(onWith(L"ETERNALVR_TEST_PE_WATER", L"off"));
    CHECK(bad.water);
    REQUIRE(bad.warnings.size() == 1);
    CHECK(bad.warnings[0].find("ETERNALVR_TEST_PE_WATER") != std::string::npos);
}

TEST_CASE("ETERNALVR_PE_EXPOSURE picks the auto-exposure image view 1 reads") {
    CHECK(read(kOn).exposure == pe::Exposure::Same);
    CHECK(read(onWith(L"ETERNALVR_PE_EXPOSURE", L"same")).exposure == pe::Exposure::Same);
    const pe::Settings prev = read(onWith(L"ETERNALVR_PE_EXPOSURE", L" Prev "));
    CHECK(prev.exposure == pe::Exposure::Prev);
    CHECK(prev.requested);
    CHECK(prev.warnings.empty());
    CHECK(read(onWith(L"ETERNALVR_PE_EXPOSURE", L"ENGINE")).exposure == pe::Exposure::Engine);
    const pe::Settings bad = read(onWith(L"ETERNALVR_PE_EXPOSURE", L"1"));
    CHECK(bad.exposure == pe::Exposure::Same);
    REQUIRE(bad.warnings.size() == 1);
    CHECK(bad.warnings[0].find("ETERNALVR_PE_EXPOSURE") != std::string::npos);
}

TEST_CASE("ETERNALVR_TEST_INSTALL_FAIL stops the install at a check or at a hook after the changes") {
    CHECK(read(kOn).testFail == pe::TestFail::None);
    CHECK(read(onWith(L"ETERNALVR_TEST_INSTALL_FAIL", L"check")).testFail == pe::TestFail::Check);
    CHECK(read(onWith(L"ETERNALVR_TEST_INSTALL_FAIL", L"redirects")).testFail == pe::TestFail::Redirects);
    CHECK(read(onWith(L"ETERNALVR_TEST_INSTALL_FAIL", L"Redirects ")).testFail == pe::TestFail::Redirects);
    CHECK(read(onWith(L"ETERNALVR_TEST_INSTALL_FAIL", L" Hook ")).testFail == pe::TestFail::Hook);
    const pe::Settings bad = read(onWith(L"ETERNALVR_TEST_INSTALL_FAIL", L"1"));
    CHECK(bad.testFail == pe::TestFail::None);
    REQUIRE(bad.warnings.size() == 1);
    CHECK(bad.warnings[0].find("ETERNALVR_TEST_INSTALL_FAIL") != std::string::npos);
}

TEST_CASE("ETERNALVR_TEST_VIEW_OFF lists parts; an unknown one is a warning") {
    const pe::Settings s = read(onWith(L"ETERNALVR_TEST_VIEW_OFF", L"edges, Screen,,volumes,bogus"));
    CHECK(s.requested);
    CHECK(s.off == (pe::kEdges | pe::kScreen | pe::kVolumes));
    REQUIRE(s.warnings.size() == 1);
    CHECK(s.warnings[0].find("bogus") != std::string::npos);
    CHECK(pe::partsText(s.off) == "edges,volumes,screen");
    CHECK(pe::partsText(0) == "none");
    CHECK(read(onWith(L"ETERNALVR_TEST_VIEW_OFF", L"dc,binds,pool,shadows,env")).off ==
          (pe::kDcCopy | pe::kBinds | pe::kPool | pe::kShadows | pe::kEnv));
}

TEST_CASE("a value that does not parse is a warning and keeps the default") {
    const pe::Settings s = read(onWith(L"ETERNALVR_TEST_EYE_COPY", L"2"));
    CHECK(s.eyeCopy == pe::EyeCopy::Screen);
    CHECK(s.warnings.size() == 1);
    CHECK(read(onWith(L"ETERNALVR_TEST_VIEW_ONLY", L"2")).viewOnly == -1);
    CHECK(read(onWith(L"ETERNALVR_TEST_VIEW_CLONES", L"all")).clones);
}

TEST_CASE("the UI layer's state is asked for only when ETERNALVR_PARALLEL_EYES is 1") {
    int asked = 0;
    const auto ui = [&asked] {
        ++asked;
        return true;
    };
    const auto none = [](std::wstring_view) -> std::optional<std::wstring> {
        return std::nullopt;
    };
    CHECK_FALSE(pe::readSettings(none, ui).requested);
    CHECK(asked == 0);
    const auto on = [](std::wstring_view name) -> std::optional<std::wstring> {
        if (name == L"ETERNALVR_PARALLEL_EYES") {
            return L"1";
        }
        if (name == L"ETERNALVR_MODE") {
            return L"stereo";
        }
        return std::nullopt;
    };
    CHECK(pe::readSettings(on, ui).requested);
    CHECK(asked == 1);
}
