// What a zoomed weapon looks like in VR (game/eternal/zoom_look.hpp): the held weapon's decls that hide the
// gun or draw a scope are written so the hands stay, no scope GUI shows and the weapon's own reticle stays
// while zoomed, and scopeZoomed tells the presenter to hold the reticle style while a scoped weapon is
// zoomed. The viewmodel hook calls noteZoomLookHands with the hands on each update; the decls are looked at
// again when the held weapon or one of its decls changes, and every kRecheckUpdates updates (a level load may
// load a decl again). The record of what was written is game::ZoomPatchBook. The game's values go back when
// the multiplayer guard trips (its trip listener, at once), when the presenter sees the guard stopped game
// touches, and when the layer shuts down. Not when the game leaves VR without the layer shutting down: the
// gun then stays and no scope GUI shows in the flat game for the rest of that run (the controllers stay
// attached once attached).

#include "vkcore/controllers.hpp"
#include "vkcore/controllers_impl.hpp"

#include "game/eternal/zoom_look.hpp"
#include "game/eternal/zoom_look_book.hpp"
#include "vkcore/game_build.hpp"
#include "vkcore/log.hpp"
#include "vkcore/mp_guard.hpp"
#include "vkcore/weapon_mods.hpp"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>

namespace evr::vkcore::controllers {

namespace {

constexpr const char* kTag = "zoom look";

// Type info (build 25216728; the Game Pass build's type info is the same for these classes). The held item's
// object and its mod decls: weapon_mods.hpp.
constexpr std::size_t kWeaponSupercharge = 0x120;    // idWeapon::superchargeParms[2], weaponDecl first
constexpr std::size_t kWeaponSuperchargeSize = 0x38; // idWeapon::superchargeParms_t
// idPlayer's zoom flags byte: zoomedIn is bit 1 (set at the zoom's start, Steam RVA 0x1462EE4, cleared at its
// end, 0x1462F34). Read on the Steam build only: on Game Pass the bit is not checked yet, so the scoped-zoom
// hold stays off there (head aim then shows the scope's ring; hand aim holds the reticle anyway).
constexpr std::size_t kPlayerZoomFlags = 0x736E;
constexpr std::uint8_t kPlayerZoomedIn = 0x2;
// scopeZoomed answers no once the viewmodel hook has not run for this long (a menu, a cutscene).
constexpr double kZoomStaleSeconds = 0.25;
constexpr int kLoggedZooms = 20; // scoped zoom starts and ends together
constexpr std::uint64_t kRecheckUpdates = 240;
constexpr int kLoggedDecls = 80;

// The decls the game may take a zoom from (zoom_look.hpp): the held decl, its secondary fire's, and per fire
// mode the mod's and the supercharge's.
using Decls = std::array<const std::byte*, 6>;

std::mutex g_mutex;
game::ZoomPatchBook g_book;        // under g_mutex
Decls g_seen{};                    // the decls last looked at (under g_mutex)
std::uint64_t g_updates = 0;       // under g_mutex
int g_logged = 0;                  // under g_mutex
bool g_loggedItemMismatch = false; // under g_mutex
bool g_loggedBuild = false;        // under g_mutex
bool g_scopedHeld = false;         // under g_mutex: one of the held decls draws a scope (the game's values)
bool g_wasScopeZoomed = false;     // under g_mutex
int g_loggedZooms = 0;             // under g_mutex
std::atomic<bool> g_scopeZoomed{false};
std::atomic<LONGLONG> g_scopeZoomedQpc{0};
std::once_flag g_listenOnce;
std::atomic<bool> g_listening{false}; // the trip listener is in: without it nothing is written

const game::ZoomLook& look() {
    static const game::ZoomLook value = [] {
        std::wstring text;
        if (!readEnv(L"ETERNALVR_ZOOM_LOOK", text)) {
            return game::ZoomLook{};
        }
        std::string narrow;
        for (const wchar_t c : text) {
            narrow.push_back(c < 0x80 ? static_cast<char>(c) : '?');
        }
        const std::optional<game::ZoomLook> parsed = game::parseZoomLook(narrow);
        if (!parsed) {
            EVR_LOG("%s: ETERNALVR_ZOOM_LOOK '%s' is not all, off or a list of hands and scope; all", kTag,
                    narrow.c_str());
            return game::ZoomLook{};
        }
        EVR_LOG("%s: ETERNALVR_ZOOM_LOOK: hands %s, scope GUI %s", kTag,
                parsed->keepHands ? "kept" : "the game's", parsed->noScopeGui ? "off" : "the game's");
        return *parsed;
    }();
    return value;
}

bool readFields(const std::byte* decl, game::ZoomDeclFields& f) {
    return safeRead(decl + game::kDeclWeaponHideHandsOnZoom, f.hideHandsOnZoom) &&
           safeRead(decl + game::kDeclWeaponHideHandsDelay, f.hideHandsDelay) &&
           safeRead(decl + game::kDeclWeaponHideHandsDelayMs, f.hideHandsDelayMs) &&
           safeRead(decl + game::kDeclWeaponScopeGuiNum, f.scopeGuiNum) &&
           safeRead(decl + game::kDeclWeaponReticle, f.reticle) &&
           safeRead(decl + game::kDeclWeaponReticleWhenZoomed, f.reticleWhenZoomed);
}

bool writeFields(const std::byte* decl, const game::ZoomDeclFields& f) {
    auto* d = const_cast<std::byte*>(decl);
    return safeCopy(d + game::kDeclWeaponHideHandsOnZoom, &f.hideHandsOnZoom, sizeof(f.hideHandsOnZoom)) &&
           safeCopy(d + game::kDeclWeaponHideHandsDelay, &f.hideHandsDelay, sizeof(f.hideHandsDelay)) &&
           safeCopy(d + game::kDeclWeaponHideHandsDelayMs, &f.hideHandsDelayMs, sizeof(f.hideHandsDelayMs)) &&
           safeCopy(d + game::kDeclWeaponScopeGuiNum, &f.scopeGuiNum, sizeof(f.scopeGuiNum)) &&
           safeCopy(d + game::kDeclWeaponReticleWhenZoomed, &f.reticleWhenZoomed,
                    sizeof(f.reticleWhenZoomed));
}

// The layer's memory access for the book: fault-safe, and writes only while the guard allows game touches
// (asked inside g_mutex right before each patch write) and the trip listener is in.
class LayerDeclIo final : public game::ZoomDeclIo {
public:
    std::string name(const void* decl) override { return itemDeclName(static_cast<const std::byte*>(decl)); }
    bool read(const void* decl, game::ZoomDeclFields& f) override {
        return readFields(static_cast<const std::byte*>(decl), f);
    }
    bool write(const void* decl, const game::ZoomDeclFields& f) override {
        return writeFields(static_cast<const std::byte*>(decl), f);
    }
    bool mayWrite() override { return g_listening.load() && mp_guard::allowsGameTouch(); }
};

// A weapon reticle decl: the layout anchor (non-null) or the zoomed one (none allowed).
bool reticleNamed(std::uintptr_t reticle) {
    return reticle && itemDeclName(reinterpret_cast<const std::byte*>(reticle)).starts_with("weaponreticle/");
}

std::string reticleName(std::uintptr_t reticle) {
    return reticle ? itemDeclName(reinterpret_cast<const std::byte*>(reticle)) : std::string("none");
}

std::string nameOr(const std::byte* decl) {
    return decl ? itemDeclName(decl) : std::string("none");
}

// One weapon decl (under g_mutex).
void patchDecl(const std::byte* decl) {
    if (!decl) {
        return;
    }
    const std::string name = itemDeclName(decl);
    if (!name.starts_with("weapon/player/")) {
        return;
    }
    game::ZoomDeclFields now;
    if (!readFields(decl, now)) {
        return;
    }
    if (!game::plausibleZoomFields(now) || !reticleNamed(now.reticle) ||
        (now.reticleWhenZoomed && !reticleNamed(now.reticleWhenZoomed))) {
        if (g_logged++ < kLoggedDecls) {
            EVR_LOG(
                "%s: '%s' does not read as a weapon decl with a reticle here (hide %u, delay %d ms, scope "
                "GUI %d); left alone",
                kTag, name.c_str(), static_cast<unsigned>(now.hideHandsOnZoom), now.hideHandsDelayMs,
                now.scopeGuiNum);
        }
        return;
    }
    LayerDeclIo io;
    const game::ZoomPatch result = g_book.patch(decl, name, now, look(), io);
    if (result != game::ZoomPatch::Written && result != game::ZoomPatch::Failed) {
        return;
    }
    if (g_logged++ < kLoggedDecls) {
        const game::ZoomDeclFields ours = game::zoomLookFields(now, look());
        EVR_LOG("%s: '%s': %shands hidden on zoom %u -> %u, after %d -> %d ms, scope GUI %d -> %d, zoomed "
                "reticle '%s' -> '%s'",
                kTag, name.c_str(),
                result == game::ZoomPatch::Failed ? "write FAILED (rolled back or kept): " : "",
                static_cast<unsigned>(now.hideHandsOnZoom), static_cast<unsigned>(ours.hideHandsOnZoom),
                now.hideHandsDelayMs, ours.hideHandsDelayMs, now.scopeGuiNum, ours.scopeGuiNum,
                reticleName(now.reticleWhenZoomed).c_str(), reticleName(ours.reticleWhenZoomed).c_str());
    }
}

// Under g_mutex: a held decl the layer changed had a scope GUI in the game's values.
bool scopedHeld(const Decls& decls) {
    for (const std::byte* decl : decls) {
        const game::ZoomDeclFields* game = decl ? g_book.gameValues(decl) : nullptr;
        if (game && game->scopeGuiNum != game::kScopeGuiNone) {
            return true;
        }
    }
    return false;
}

// Under g_mutex: the player's zoom flag, on the Steam build only (kPlayerZoomFlags).
bool playerZoomed(const std::byte* player) {
    const GameBuild* build = currentGameBuild();
    if (!build || build->kind != GameBuildKind::Steam) {
        if (!g_loggedBuild) {
            g_loggedBuild = true;
            EVR_LOG("%s: %s: the zoom flag is not checked on this build; no reticle hold for a scoped zoom",
                    kTag, build ? build->name : "unknown build");
        }
        return false;
    }
    std::uint8_t flags = 0;
    return player && safeRead(player + kPlayerZoomFlags, flags) && (flags & kPlayerZoomedIn) != 0;
}

// Under g_mutex, each update: the scoped zoom for the presenter's reticle hold, logged at its start and end.
void noteScopeZoom(const std::byte* player) {
    const bool scopeZoomed = game::scopeReticleHold(playerZoomed(player), g_scopedHeld, look());
    g_scopeZoomedQpc.store(nowQpc(), std::memory_order_relaxed);
    g_scopeZoomed.store(scopeZoomed, std::memory_order_relaxed);
    if (scopeZoomed != g_wasScopeZoomed) {
        g_wasScopeZoomed = scopeZoomed;
        if (g_loggedZooms++ < kLoggedZooms) {
            EVR_LOG("%s: scoped zoom %s: the reticle style %s", kTag, scopeZoomed ? "starts" : "ends",
                    scopeZoomed ? "held at none (no scope ring)" : "as before the zoom");
        }
    }
}

void restoreOnTrip() {
    restoreZoomLook("the multiplayer guard tripped");
}

} // namespace

void noteZoomLookHands(const std::byte* hands, const std::byte* player) {
    const game::ZoomLook& wanted = look();
    if (!wanted.keepHands && !wanted.noScopeGui) {
        return;
    }
    // Never with g_mutex held: after a trip the listener runs at once and takes it.
    std::call_once(g_listenOnce, [] {
        g_listening = mp_guard::addTripListener(&restoreOnTrip);
        if (!g_listening) {
            EVR_LOG("%s: no room in the multiplayer guard's trip listeners; no weapon decl is changed", kTag);
        }
    });
    std::lock_guard lock(g_mutex);
    if (g_book.stopped()) {
        return;
    }
    Decls decls{heldItemDecl(hands)};
    if (decls[0]) {
        safeRead(decls[0] + game::kDeclWeaponSecondaryFireDecl, decls[1]);
    }
    // The held item's object, checked by its decl. No object (equipment such as a grenade) is no mismatch.
    const std::byte* weapon = nullptr;
    const std::byte* weaponDecl = nullptr;
    if (decls[0] && safeRead(hands + kHandsItemObject, weapon) && weapon) {
        if (safeRead(weapon + kInventoryItemDecl, weaponDecl) && weaponDecl == decls[0]) {
            const ModDecls mods = weaponModDecls(weapon);
            for (std::size_t i = 0; i < 2; ++i) {
                decls[2 + i] = mods[i];
                safeRead(weapon + kWeaponSupercharge + i * kWeaponSuperchargeSize, decls[4 + i]);
            }
        } else if (!g_loggedItemMismatch) {
            g_loggedItemMismatch = true;
            EVR_LOG("%s: the held item's object does not name the held decl ('%s'); its mod decls are not "
                    "changed",
                    kTag, itemDeclName(decls[0]).c_str());
        }
    }
    if (decls == g_seen && ++g_updates % kRecheckUpdates != 0) {
        noteScopeZoom(player);
        return;
    }
    if (decls != g_seen && g_logged++ < kLoggedDecls) {
        EVR_LOG("%s: held '%s' (secondary fire '%s'), mod decls '%s', '%s', supercharge '%s', '%s'", kTag,
                nameOr(decls[0]).c_str(), nameOr(decls[1]).c_str(), nameOr(decls[2]).c_str(),
                nameOr(decls[3]).c_str(), nameOr(decls[4]).c_str(), nameOr(decls[5]).c_str());
    }
    g_seen = decls;
    for (const std::byte* decl : decls) {
        patchDecl(decl);
    }
    g_scopedHeld = scopedHeld(decls);
    noteScopeZoom(player);
}

bool scopeZoomed() {
    return g_scopeZoomed.load(std::memory_order_relaxed) &&
           secondsSince(g_scopeZoomedQpc.load(std::memory_order_relaxed)) < kZoomStaleSeconds;
}

void restoreZoomLook(const char* why) {
    std::lock_guard lock(g_mutex);
    g_scopeZoomed.store(false, std::memory_order_relaxed);
    if (g_book.stopped()) {
        return;
    }
    LayerDeclIo io;
    const game::ZoomPatchBook::Restored r = g_book.restore(io);
    if (r.total > 0) {
        EVR_LOG("%s: the game's values back in %d of %d weapon decl(s) (%s)", kTag, r.back, r.total, why);
    }
}

} // namespace evr::vkcore::controllers
