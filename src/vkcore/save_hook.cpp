// The game's own checkpoint saves, for the stall lines (stall_watch.hpp, docs/VR_STEREO.md "Stalls").
//
// A checkpoint save holds the game's thread for a few hundred milliseconds (the game serialises the map),
// flat or in VR, so a stall right after a fight or before a cutscene is usually one. The save is
// SaveCheckPointAndGetFiles (RVA 0x6A8B80 in Steam build 25216728, named by its own log line): past its
// early outs (no map, no save slot) it logs "--------------SAVEGAME----------- time: N" and then
// "SaveCheckPointAndGetFiles\n---\n" (qconsole.log). The mid hook sits on the lea of that second string
// (RVA 0x6A8C6E), found by the string itself, so only a save that is really made counts. It only adds to
// two counters; nothing in the game is read or changed.

#include "vkcore/stall_watch.hpp"

#include "vkcore/game_text.hpp"
#include "vkcore/log.hpp"
#include "vkcore/mid_hook.hpp"

#include <cstddef>
#include <string>
#include <vector>

namespace evr::vkcore::stall_watch {

namespace {

constexpr const char* kTag = "stall";
constexpr const char* kSaveText = "SaveCheckPointAndGetFiles\n---\n";

void onSave(const HookRegisters& /*registers*/) {
    onCheckpointSave();
}

// lea rcx, [rip + disp32]
bool isLeaRcx(const std::byte* at) {
    return at[0] == std::byte{0x48} && at[1] == std::byte{0x8D} && at[2] == std::byte{0x0D};
}

} // namespace

bool installCheckpointSaveHook() {
    GameImage image;
    if (!locateGameImage(image, kTag)) {
        return false;
    }
    const std::byte* text = findUniqueString(image, kSaveText);
    const std::vector<const std::byte*> refs =
        text ? findLeaReferences(image, text) : std::vector<const std::byte*>{};
    if (refs.size() != 1 || !isLeaRcx(refs.front()) || functionStart(image, refs.front()) == nullptr) {
        EVR_LOG(
            "%s: the game's checkpoint save was not found (string %s, %zu reference(s)); stall lines do not "
            "name saves",
            kTag, text ? "found" : "missing", refs.size());
        return false;
    }
    const std::byte* site = refs.front();
    std::string error;
    if (!installMidHook(const_cast<std::byte*>(site), &onSave, error)) {
        EVR_LOG("%s: checkpoint save hook at RVA 0x%X failed: %s; stall lines do not name saves", kTag,
                image.rva(site), error.c_str());
        return false;
    }
    EVR_LOG("%s: checkpoint save hook at RVA 0x%X (SaveCheckPointAndGetFiles at RVA 0x%X)", kTag,
            image.rva(site), image.rva(functionStart(image, site)));
    return true;
}

} // namespace evr::vkcore::stall_watch
