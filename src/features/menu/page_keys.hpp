#pragma once

// A screen's own previous and next keys inside one of its tabs, which the tab keys (Q / E, the grips) do not
// reach: the mission-complete screen's Rewards tab turns its pages with A and D ("A 1/2 D" under the
// rewards), and the Dossier's Arsenal, Codex and Challenges pages switch their inner tabs (the weapons, the
// categories, the challenge lists) with A and D. A pad does both with its triggers and the tabs with its
// shoulders. No controller input sent A or D there: on the Rewards tab only pointing at an arrow and pulling
// the trigger turned a page (rig 2026-10-17). While such a screen is up a stick pushed left or right sends A
// and D instead of the tab keys, and a stick click sends the left stick's A or the right stick's D
// (menu_router.hpp); the grips keep the tabs, as the pad's shoulders do. The screens name the sticks for A
// and D in short ("LS 1/2 RS", vkcore/prompt_page_keys.hpp): they have room for about two letters where the
// game writes A and D ("RS Left" read "..." on the rig).
//
// The screens count as up while the game runs their updates (hooked in vkcore/prompt_page_keys.cpp): the
// Rewards tab's update with its arrows up (more than one page; with one the arrows are hidden and the stick
// keeps the tabs), or the update of a visible tab list on A / D with two tabs or more, the last run within
// `updateFresh`, and the game may be touched.
//
// Pure; the presenter asks once per XR frame.

#include <cstdint>

namespace evr::menu {

struct PageKeys {
    std::uint8_t previous = 0; // virtual-key codes; 0: none
    std::uint8_t next = 0;

    [[nodiscard]] bool any() const { return previous != 0 && next != 0; }
    friend bool operator==(const PageKeys&, const PageKeys&) = default;
};

// A and D: the Rewards tab's page arrows and the Dossier pages' inner tabs.
inline constexpr PageKeys kPageArrowKeys{'A', 'D'};

struct PageKeysTuning {
    double updateFresh = 0.25; // an update's last run counts this long (about 20 game frames at 90 fps)
};

struct PageKeysInput {
    double sinceRewardsUpdate = -1.0; // seconds since the Rewards tab's update last ran; negative: never
    bool rewardsArrows = false;       // its arrows were up then
    // Seconds since a visible tab list on A / D with two tabs or more last updated; negative: never.
    double sinceTabListUpdate = -1.0;
    bool allowed = true; // the game may be touched (the multiplayer guard)
};

// The Rewards tab is up with its arrows.
bool rewardsShowing(const PageKeysInput& in, const PageKeysTuning& tuning = {});

// The page keys of the screen up now, or none.
PageKeys pageKeysFor(const PageKeysInput& in, const PageKeysTuning& tuning = {});

} // namespace evr::menu
