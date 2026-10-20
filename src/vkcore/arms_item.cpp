// The item the game's arms hold as it animates them, once per tick of the hands (controllers_impl.hpp,
// ArmsTick; input::armsItem).

#include "features/input/offhand_policy.hpp"
#include "vkcore/arm_signals.hpp"
#include "vkcore/controllers_impl.hpp"
#include "vkcore/log.hpp"

#include <cstddef>
#include <mutex>
#include <string>
#include <string_view>

namespace evr::vkcore::controllers {

namespace {

constexpr const char* kTag = "controllers";

// Type info (build 25216728).
constexpr std::size_t kHandsOwner = 0x358;              // idHands::owner
constexpr std::size_t kPlayerWeaponFistsSwim = 0x4D188; // idPlayer::weaponFistsSwim (swim fists decl)
constexpr std::string_view kSwimFists = "weapon/player/fists_swim";
// A tick kept longer than this ago is read again (the viewmodel hook not running for these hands).
constexpr double kStaleSeconds = 0.1;
constexpr int kMaxLines = 40;

// Everything below, for the hands `g_hands`: new hands (a level load) may give a decl's address to another
// item, so the decls known by name are forgotten with them.
std::mutex g_mutex;
const std::byte* g_hands = nullptr;
// The player's swim fists (idPlayer::weaponFistsSwim) once its name has read as theirs; one that reads as
// another item's is never used (logged).
const std::byte* g_swimFistsDecl = nullptr;
const std::byte* g_notSwimFistsDecl = nullptr;
// Whether the held decl `g_swimNameDecl` is named as the swim fists.
const std::byte* g_swimNameDecl = nullptr;
bool g_swimNameIs = false;
// The last tick read, and when.
ArmsTick g_tick;
LONGLONG g_tickQpc = 0;
int g_lines = 0;
int g_swimLines = 0;

bool lineDue() {
    if (g_lines >= kMaxLines) {
        return false;
    }
    ++g_lines;
    return true;
}

void forHands(const std::byte* hands) {
    if (hands == g_hands) {
        return;
    }
    g_hands = hands;
    g_swimFistsDecl = nullptr;
    g_notSwimFistsDecl = nullptr;
    g_swimNameDecl = nullptr;
    g_swimNameIs = false;
    g_tick = ArmsTick{};
    g_tickQpc = 0;
}

const std::byte* playerSwimFists(const std::byte* hands) {
    const std::byte* owner = nullptr;
    const std::byte* swim = nullptr;
    if (!safeRead(hands + kHandsOwner, owner) || !owner || !safeRead(owner + kPlayerWeaponFistsSwim, swim) ||
        !swim || swim == g_notSwimFistsDecl) {
        return nullptr;
    }
    if (swim == g_swimFistsDecl) {
        return swim;
    }
    const std::string name = itemDeclName(swim);
    if (!name.starts_with(kSwimFists)) {
        g_notSwimFistsDecl = swim;
        if (lineDue()) {
            EVR_LOG("%s: idPlayer::weaponFistsSwim reads as '%s', not the swim fists: the arms keep the held "
                    "item's row",
                    kTag, name.c_str());
        }
        return nullptr;
    }
    g_swimFistsDecl = swim;
    return swim;
}

// Whether `held` is the swim fists: the player's own pointer, or a decl of that name (logged, as the grips
// then go by the held one's).
bool isSwimFists(const std::byte* held, const std::byte* swimFists) {
    if (!held || held == swimFists) {
        return held != nullptr;
    }
    if (held != g_swimNameDecl) {
        g_swimNameDecl = held;
        g_swimNameIs = itemDeclName(held).starts_with(kSwimFists);
        if (g_swimNameIs && swimFists && lineDue()) {
            EVR_LOG(
                "%s: the swim fists held are not idPlayer::weaponFistsSwim's decl; the held one's grips are "
                "used",
                kTag);
        }
    }
    return g_swimNameIs;
}

ArmsTick readLocked(const std::byte* hands) {
    forHands(hands);
    ArmsTick t;
    t.hands = hands;
    t.held = heldItemDecl(hands);
    t.handsState = readDestHandsState(hands);
    // An unread state counts as HS_NONE (-1): not the swim's.
    t.swim = hands && input::handsSwim(t.handsState.value_or(-1), readHandsFlags(hands));
    t.arms = t.held;
    if (t.swim) {
        // The swim fists held are their own pointer, which the grips read while they are held are kept by.
        const std::byte* swimFists = playerSwimFists(hands);
        t.arms = static_cast<const std::byte*>(
            input::armsItem(t.held, isSwimFists(t.held, swimFists), true, swimFists));
    }
    const float dt = g_tickQpc != 0 ? static_cast<float>(secondsSince(g_tickQpc)) : 0.0f;
    t.sinceSwim = input::secondsSinceSwim(g_tick.sinceSwim, t.swim, dt);
    // A dive (a swim after kSwimGripHoldOffSeconds without one) and a way out (that long without a swim since
    // the last): the hands' signal also clears for a tick or a few at each change of the held item in the
    // water.
    const float hold = input::kSwimGripHoldOffSeconds;
    const bool dive = t.swim && !g_tick.swim && g_tick.sinceSwim >= hold;
    const bool out = !t.swim && g_tick.sinceSwim < hold && t.sinceSwim >= hold;
    if (g_tickQpc != 0 && (dive || out) && g_swimLines < kMaxLines) {
        ++g_swimLines;
        const std::string held = t.held ? itemDeclName(t.held) : std::string{};
        if (dive) {
            EVR_LOG("%s: the hands swim ('%s' held)", kTag, held.c_str());
        } else {
            EVR_LOG(
                "%s: the hands have not swum for %.1f s ('%s' held): the arms' grips of the game's are read "
                "again",
                kTag, static_cast<double>(hold), held.c_str());
        }
    }
    g_tick = t;
    g_tickQpc = nowQpc();
    return t;
}

} // namespace

ArmsTick noteArmsTick(const std::byte* hands) {
    std::lock_guard lock(g_mutex);
    return readLocked(hands);
}

ArmsTick armsTick(const std::byte* hands) {
    std::lock_guard lock(g_mutex);
    if (hands && hands == g_tick.hands && g_tickQpc != 0 && secondsSince(g_tickQpc) < kStaleSeconds) {
        return g_tick;
    }
    return readLocked(hands);
}

} // namespace evr::vkcore::controllers
