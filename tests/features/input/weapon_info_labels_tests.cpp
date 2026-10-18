#include "features/input/binding_compiler.hpp"
#include "features/input/button_labels.hpp"
#include "features/input/controller_bindings.hpp"
#include "features/input/prompt_keys.hpp"
#include "game/eternal/controller_data.hpp"

#include <doctest/doctest.h>

#include <cstddef>
#include <cstdint>
#include <ostream>
#include <string>

using evr::game::builtinControllerData;
using evr::game::Controller;
using evr::game::controllerName;
using evr::game::GameAction;
using evr::game::Handedness;
using evr::game::kControllers;
using evr::input::buildBindingProfile;
using evr::input::buttonLabelsFor;
using evr::input::ControllerData;
using evr::input::HudKeyCallers;
using evr::input::hudKeyForm;
using evr::input::HudKeyForm;
using evr::input::hudKeyText;
using evr::input::parseControllerData;
using evr::input::PromptLabelSet;
using evr::input::promptLabelSet;

// The HUD's weapon info row (equipment, Flame Belch, chainsaw above their icons) gets the short texts. In a
// 2048x2208 GUI capture its three label boxes were each sized to "Left Grip", "Right Stick Up" and "Left
// Trigger" cut to about 64 px of text (boxes about 70 px wide), centred on icons 59.5 px apart, so they
// overlapped into one bar that read "Left Grip ght St... ft Tr...".

namespace {

PromptLabelSet labelSet(Controller controller, Handedness handedness = Handedness::Right) {
    const ControllerData data = parseControllerData(builtinControllerData(controller));
    return promptLabelSet(buildBindingProfile(data.maps.at(handedness)).profile, buttonLabelsFor(data));
}

std::size_t slotOf(const PromptLabelSet& set, GameAction action) {
    const int slot = set.slot[static_cast<std::size_t>(action)];
    REQUIRE(slot >= 0);
    return static_cast<std::size_t>(slot);
}

// The short text the label set holds for an action's slot.
std::string shortText(const PromptLabelSet& set, GameAction action) {
    const std::size_t slot = slotOf(set, action);
    REQUIRE(slot < set.distinctShort.size());
    return set.distinctShort[slot];
}

// The return addresses found in build 25216728 at an image base: the HUD helper's (0xF0BB50) LocalizedKeyName
// call (0xF0BDA1) and the weapon info update's (0xF0BF60) seven calls to the helper: equipment, Flame Belch,
// chainsaw, the Crucible three times, switch equipment.
constexpr std::uintptr_t kBase = 0x7FF7ACDB0000;
HudKeyCallers steamCallers() {
    HudKeyCallers callers;
    callers.helperKeyText = kBase + 0xF0BDA6;
    callers.weaponInfo = {kBase + 0xF0C01C, kBase + 0xF0C043, kBase + 0xF0C06A, kBase + 0xF0C091,
                          kBase + 0xF0C0B8, kBase + 0xF0C0DF, kBase + 0xF0C16F, 0};
    return callers;
}

// What the LocalizedKeyName detour gives for an action's VR key, asked for from `returnAddress` by code that
// `callerReturn` returns to.
std::string answer(const PromptLabelSet& set,
                   GameAction action,
                   std::uintptr_t returnAddress,
                   std::uintptr_t callerReturn,
                   const HudKeyCallers& callers) {
    const std::string* text =
        hudKeyText(slotOf(set, action), hudKeyForm(returnAddress, callerReturn, callers), set);
    REQUIRE(text != nullptr);
    return *text;
}

} // namespace

TEST_CASE("the weapon info row's short texts: initials, one-word names whole, the press in words") {
    const PromptLabelSet touch = labelSet(Controller::OculusTouch);
    CHECK(shortText(touch, GameAction::Equipment) == "LT");
    CHECK(shortText(touch, GameAction::FlameBelch) == "LG");
    CHECK(shortText(touch, GameAction::Chainsaw) == "RS Up");
    CHECK(shortText(touch, GameAction::Crucible) == "LSC");
    CHECK(shortText(touch, GameAction::SwitchEquipment) == "X");
    CHECK(shortText(touch, GameAction::Dossier) == "Hold X");
    const PromptLabelSet swapped = labelSet(Controller::OculusTouch, Handedness::LeftButtonSwap);
    CHECK(shortText(swapped, GameAction::Equipment) == "RT");
    CHECK(shortText(swapped, GameAction::FlameBelch) == "RG");
    // Vive wands carry Flame Belch on holding the trigger whose tap is the equipment.
    const PromptLabelSet wand = labelSet(Controller::ViveWand);
    CHECK(shortText(wand, GameAction::Equipment) == "LT");
    CHECK(shortText(wand, GameAction::FlameBelch) == "Hold LT");
    // Index controllers have an A on each hand.
    CHECK(shortText(labelSet(Controller::ValveIndex), GameAction::SwitchEquipment) == "LA");
    CHECK(shortText(labelSet(Controller::WindowsMixedReality), GameAction::Crucible) == "Hold LSC");
}

TEST_CASE("every distinct text has its short text in the same slot") {
    for (const Controller controller : kControllers) {
        const ControllerData data = parseControllerData(builtinControllerData(controller));
        for (const auto& [handedness, entries] : data.maps) {
            const PromptLabelSet set =
                promptLabelSet(buildBindingProfile(entries).profile, buttonLabelsFor(data));
            INFO(controllerName(controller));
            REQUIRE(set.distinctShort.size() == set.distinct.size());
            for (std::size_t slot = 0; slot < set.distinct.size(); ++slot) {
                CHECK_FALSE(set.distinctShort[slot].empty());
                CHECK(set.distinctShort[slot].size() <= set.distinct[slot].size());
            }
        }
    }
}

TEST_CASE("every built-in map's weapon info row fits its boxes") {
    // In the same capture with the short texts, the boxes' centres are 59 px apart and a box is about 5.5 px
    // plus 8.5 px a character ("LG" 23 px, "LT" 22 px, "RS Up" 48 px), so two neighbours stay apart while
    // their texts hold 12 characters between them. Left to right: Flame Belch, chainsaw, equipment.
    const auto length = [](const PromptLabelSet& set, GameAction action) {
        const int slot = set.slot[static_cast<std::size_t>(action)];
        return slot < 0 ? std::size_t{0} : set.distinctShort.at(static_cast<std::size_t>(slot)).size();
    };
    for (const Controller controller : kControllers) {
        const ControllerData data = parseControllerData(builtinControllerData(controller));
        for (const auto& [handedness, entries] : data.maps) {
            const PromptLabelSet set =
                promptLabelSet(buildBindingProfile(entries).profile, buttonLabelsFor(data));
            INFO(controllerName(controller), " ", static_cast<int>(handedness));
            CHECK(length(set, GameAction::FlameBelch) + length(set, GameAction::Chainsaw) <= 12);
            CHECK(length(set, GameAction::Chainsaw) + length(set, GameAction::Equipment) <= 12);
        }
    }
}

TEST_CASE("the weapon info update's calls to the HUD helper get the short texts") {
    const HudKeyCallers callers = steamCallers();
    const PromptLabelSet set = labelSet(Controller::OculusTouch);
    const std::uintptr_t helper = callers.helperKeyText;
    // The row's three labels, each from its own call ("Left Trigger", "Left Grip", "Right Stick Up" before).
    CHECK(answer(set, GameAction::Equipment, helper, kBase + 0xF0C01C, callers) == "LT");
    CHECK(answer(set, GameAction::FlameBelch, helper, kBase + 0xF0C043, callers) == "LG");
    CHECK(answer(set, GameAction::Chainsaw, helper, kBase + 0xF0C06A, callers) == "RS Up");
    CHECK(answer(set, GameAction::Crucible, helper, kBase + 0xF0C091, callers) == "LSC");
    CHECK(answer(set, GameAction::SwitchEquipment, helper, kBase + 0xF0C16F, callers) == "X");
    for (const std::uintptr_t call : callers.weaponInfo) {
        if (call != 0) {
            CHECK(hudKeyForm(helper, call, callers) == HudKeyForm::Short);
        }
    }
}

TEST_CASE("every other HUD label keeps the full text") {
    const HudKeyCallers callers = steamCallers();
    const PromptLabelSet set = labelSet(Controller::OculusTouch);
    const std::uintptr_t helper = callers.helperKeyText;
    // The helper's other callers: the demon's ability list (0xEC41ED), 0xEC921F, the weapon info's reload
    // hint (0xF0CC37) and the tutorial objectives (0xFAEF6F).
    for (const std::uintptr_t other :
         {kBase + 0xEC41ED, kBase + 0xEC921F, kBase + 0xF0CC37, kBase + 0xFAEF6F}) {
        CHECK(hudKeyForm(helper, other, callers) == HudKeyForm::Full);
        CHECK(answer(set, GameAction::FlameBelch, helper, other, callers) == "Left Grip");
    }
    // The other HUD helpers' LocalizedKeyName calls (blood punch and dash 0xED061A, the reticle 0xFA137D) and
    // the tooltip text pass's (0x18366F4), whatever a frame above them returns to.
    for (const std::uintptr_t other : {kBase + 0xED061A, kBase + 0xFA137D, kBase + 0x18366F4}) {
        CHECK(hudKeyForm(other, kBase + 0xF0C043, callers) == HudKeyForm::Full);
        CHECK(answer(set, GameAction::FlameBelch, other, kBase + 0xF0C043, callers) == "Left Grip");
    }
    // The stack held no frame above the helper's.
    CHECK(hudKeyForm(helper, 0, callers) == HudKeyForm::Full);
    // Nothing found at install answers nothing in short.
    CHECK(hudKeyForm(0, 0, HudKeyCallers{}) == HudKeyForm::Full);
    CHECK(hudKeyForm(helper, kBase + 0xF0C043, HudKeyCallers{}) == HudKeyForm::Full);
}

TEST_CASE("a slot past the texts has no HUD text") {
    const PromptLabelSet set = labelSet(Controller::OculusTouch);
    CHECK(hudKeyText(set.distinct.size(), HudKeyForm::Full, set) == nullptr);
    CHECK(hudKeyText(set.distinct.size(), HudKeyForm::Short, set) == nullptr);
    CHECK(hudKeyText(0, HudKeyForm::Full, PromptLabelSet{}) == nullptr);
}
