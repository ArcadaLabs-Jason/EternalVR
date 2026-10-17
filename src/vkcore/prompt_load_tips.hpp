#pragma once

// The loading screen's tips bar ("[Q] PREVIOUS | [E] NEXT", features/menu/load_tips_keys.hpp). Its update
// (the function that loads "#str_code_loadscreen_previous_GHOST47115", 0x456BF0 in build 25216728, 0x4665A0
// in Game Pass 1.0.56.0) polls Q / E and the pad's shoulders on every frame of the loading screen (`mov r8d,
// 0x104` at 0x45824B, the shoulder's poll after Q's), then writes each half of the bar as "K_%s %s" from the
// key's name and the localized label, which the text pass turns into "[Q] PREVIOUS". Three hooks:
// - at the shoulder's poll: the bar polled its keys now, so the grips send them (updateLoadTipsKeys);
// - at the platform compare (`cmp eax, [r13+0x544]` at 0x45869D, 0x46805D in Game Pass): a bar written before
//   the controllers' names were known (a game started straight into a map: rig 2026-10-17, the bar at 11.2 s,
//   the names at 11.75 s) is written again, by setting the platform it was written for to none through the
//   register the compare reads;
// - at the two calls that format the halves (0x4587E1, 0x4589A5; each the first `mov r8, rax; call` after its
//   label's string): a key-token format is replaced by "[<button>] <label>" with the text the key name
//   dispatch gives the tips bar (input::keyNameAnswer, KeyNameCallers::loadTips: the grips' names).
// Each part is optional and checked at install; a part not found leaves the game's keys and text there.

#include "features/input/button_labels.hpp"
#include "features/input/controller_state.hpp"
#include "vkcore/game_text.hpp"

#include <memory>
#include <optional>

namespace evr::vkcore::controllers {

// The prompt texts published last (prompt_hooks.cpp), or null before the first.
std::shared_ptr<const input::PromptLabelSet> currentPromptLabels();

// From installPromptHooks, once the KeyNameText dispatch is set up.
void installLoadTipsHooks(const GameImage& image, const char* tag);

// XR worker, every frame: the grips send Q / E while the tips bar is up (menu::tipsBarShowing: polled a
// moment ago, `menuUp` false, and play has not begun: the game rendered no head-tracked view since the bar's
// last poll and `shownGameView`, the frame on the headset, is none); every key down goes up when the bar is
// gone. Logs once a load the gap from the bar's last poll to the game's first view.
void updateLoadTipsKeys(const std::optional<input::InputFrame>& frame,
                        double seconds,
                        bool menuUp,
                        bool shownGameView);

// XR worker, when its frames stop (VR stops, the session ends, the headset shows no frames): every key down
// goes up.
void releaseLoadTipsKeys();

} // namespace evr::vkcore::controllers
