#include "vkcore/parallel_eyes_settings.hpp"

#include <doctest/doctest.h>

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

TEST_CASE("not with DLSS: Route S runs it per eye") {
    for (const wchar_t* on : {L"1", L"true", L"ON"}) {
        const pe::Settings s = read(onWith(L"ETERNALVR_STEREO_DLSS", on));
        CHECK_FALSE(s.requested);
        CHECK(s.why.find("DLSS") != std::string::npos);
    }
    CHECK(read(onWith(L"ETERNALVR_STEREO_DLSS", L"0")).requested);
    CHECK(read(onWith(L"ETERNALVR_STEREO_DLSS", L"")).requested);
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
    CHECK(read(onWith(L"ETERNALVR_TEST_VIEW_ONLY", L"1")).viewOnly == 1);
    // Without the clones eye 1 has no image of its own.
    auto noClones = onWith(L"ETERNALVR_TEST_VIEW_CLONES", L"0");
    noClones[L"ETERNALVR_TEST_EYE_COPY"] = L"1";
    CHECK(read(noClones).eyeCopy == pe::EyeCopy::Off);
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
