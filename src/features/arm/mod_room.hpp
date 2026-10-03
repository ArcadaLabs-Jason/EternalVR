#pragma once

// Room for the layer's joint modifiers in the hands' two modifier lists (docs/VR_HANDS_HUD.md, "The whole
// arm"; vkcore/offhand_mods.cpp).
//
// The hands keep their joint modifiers in two idList<jointMod_t>, one per generation, that hold the same
// count. idHands::InitJointMods builds them on a node it has just taken from the animation pool and adds
// its three attach-joint modifiers with AddJointMod, which grows both lists by exactly one through the
// node's SetNum; so they end full (3 in use, room for 3), and appending in place is impossible. SetNum
// grows a list with the game's own allocator (idList Resize: a new block, the old entries copied, the old
// block freed) and lowers a count without touching the memory. So, right after the game's three
// AddJointMod calls and still inside InitJointMods, the layer asks SetNum for its six more per arm it moves
// (the off hand's, the weapon arm's or both) and then for the old count again: the room is the game's, and
// the count the game sees is unchanged. The layer's entries are appended into that room later, in place
// (vkcore/game_arm.cpp).
// [static: InitJointMods 0x138B080, AddJointMod 0x138B360, SetNum 0x19A61F0, Resize 0x4AAB80; build
// 25216728]
//
// SetNum compares the new count with the first list's only and returns when they are equal, so when the
// first list could not grow but the second did, lowering the count again leaves the second list's alone;
// restoreCount says so. Pure: the counts come in, the decisions go out.

#include <cstdint>

namespace evr::arm {

// One list's int num and int size.
struct ModListCounts {
    std::int32_t num = 0;
    std::int32_t size = 0;
};

// More than this in either list is not the hands' list.
inline constexpr std::int32_t kMaxJointMods = 256;

// The layer's modifiers for one arm: the forearm's four roll joints, the elbow and the shoulder end.
inline constexpr std::int32_t kModsPerArm = 6;

// The room to make for `arms` arms (0, 1 or 2; anything else counts as none).
constexpr std::int32_t modsForArms(std::int32_t arms) {
    return arms == 1 || arms == 2 ? arms * kModsPerArm : 0;
}

// Both lists sane (0 <= num <= size <= kMaxJointMods) and holding the same count.
bool modListsAgree(ModListCounts first, ModListCounts second);

// Whether `extra` more entries fit past the count in both lists.
bool roomFor(ModListCounts first, ModListCounts second, std::int32_t extra);

// What to ask SetNum for so both lists have room for `extra` more.
struct RoomPlan {
    enum class Step : std::uint8_t {
        Refuse, // the lists do not agree, or the count would pass kMaxJointMods
        Enough, // the room is there already
        Grow,   // SetNum(growTo), then SetNum(num)
    };
    Step step = Step::Refuse;
    std::int32_t num = 0;
    std::int32_t growTo = 0;
};
RoomPlan planRoom(ModListCounts first, ModListCounts second, std::int32_t extra);

// A list after SetNum(growTo) and SetNum(num) whose count must be written back to `num` (SetNum's early
// return above): its count is past `num` but within its size.
bool restoreCount(ModListCounts list, std::int32_t num);

// The room was made: both lists hold `num` again, with room for `extra` more.
bool roomMade(ModListCounts first, ModListCounts second, std::int32_t num, std::int32_t extra);

} // namespace evr::arm
