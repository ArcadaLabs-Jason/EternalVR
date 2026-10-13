#include "vkcore/profile_hooks.hpp"

#include "platform/mp_policy/profile_cvars.hpp"
#include "vkcore/cvar_book.hpp"
#include "vkcore/game_text.hpp"
#include "vkcore/log.hpp"
#include "vkcore/mid_hook.hpp"
#include "vkcore/mp_guard.hpp"
#include "vkcore/runtime_cvars.hpp"
#include "vkcore/status_file.hpp"
#include "vkcore/taa_hooks.hpp"
#include "vkcore/taa_locate.hpp"
#include "vkcore/view_slots.hpp"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace evr::vkcore {

namespace {

constexpr const char* kTag = "profile";

// The serializer (RVA 0x141D040; Game Pass 0x14207B0): `push rbp; push r14; push r15; lea rbp, [rsp -
// 0x4050]; mov eax, 0x4150; call` the stack probe, the security cookie, then `cmp byte ptr [rdx], 0` (load or
// save) and `mov r14, rdx`. The detour moves the first 14 bytes (the pushes and the lea), none RIP-relative.
constexpr const char* kSerializerSignature =
    "40 55 41 56 41 57 48 8D AC 24 B0 BF FF FF B8 50 41 00 00 E8 ?? ?? ?? ?? 48 2B E0 48 8B 05 ?? ?? ?? ?? "
    "48 33 C4 48 89 85 10 40 00 00 80 3A 00 4C 8B F2";
// view_skipShakes' save in it (+0x1A8F in both builds): `mov rdx, [cvar]; test dword ptr [rdx + 0x40],
// 0x30000`.
constexpr const char* kArchiveCheck = "48 8B 15 ?? ?? ?? ?? F7 42 40 00 00 03 00";
constexpr std::size_t kArchiveCheckSpan = 0x4000;
constexpr int kLoggedCalls = 20;

// (settings, context: its first byte 0 for a load, the flag the caller passes in r8; r9 passed through).
using SerializeFn = void (*)(void* settings, std::uint8_t* context, std::uintptr_t flag, std::uintptr_t r9);

struct ProfileCvar {
    std::string_view name;
    std::byte* object = nullptr;
    std::optional<std::string> loaded; // its value right after the last profile load (under heldMutex)
};

SerializeFn g_original = nullptr;
cvar_book::SetStringFn g_set = nullptr;
std::vector<ProfileCvar> g_cvars; // filled before the hook goes in, then only `loaded` changes
std::once_flag g_installOnce;
std::atomic<int> g_calls{0};
// The one-time repair (mp_policy::profileRepair), decided at the first load; under heldMutex.
bool g_repairAsked = false;
bool g_loadSeen = false;
bool g_repairWritten = false; // the repair's 0 is in the cvar, for the next save
mp_policy::ProfileRepair g_repair = mp_policy::ProfileRepair::Off;

// A save swapped in the player's value: the held value to put back after it.
struct Swap {
    const ProfileCvar* cvar;
    std::string held;
};

// runtime_cvars::setProfileIo for one serializer call: begin() and end() under heldMutex; if a throw skips
// end(), the destructor takes the lock and ends it, so the profile cvars are held again.
class ProfileIo {
public:
    ProfileIo() = default;
    ProfileIo(const ProfileIo&) = delete;
    ProfileIo& operator=(const ProfileIo&) = delete;
    ~ProfileIo() {
        if (m_on) {
            std::lock_guard lock(runtime_cvars::heldMutex());
            runtime_cvars::setProfileIo(false);
        }
    }
    void begin() {
        runtime_cvars::setProfileIo(true);
        m_on = true;
    }
    void end() {
        runtime_cvars::setProfileIo(false);
        m_on = false;
    }

private:
    bool m_on = false;
};

bool logCall() {
    return g_calls.fetch_add(1) < kLoggedCalls;
}

ProfileCvar* named(std::string_view name) {
    for (ProfileCvar& c : g_cvars) {
        if (c.name == name) {
            return &c;
        }
    }
    return nullptr;
}

void reportRepair(bool savedSince) {
    if (const char* value = mp_policy::profileRepairStatus(g_repair, savedSince)) {
        status::field("profile_repair", value);
    }
}

// Under heldMutex, before the load.
void beforeLoad() {
    for (ProfileCvar& c : g_cvars) {
        const std::string live = cvar_book::valueText(c.object);
        if (const auto seed = mp_policy::profileLoadSeed(live, cvar_book::valueBefore(c.name))) {
            g_set(c.object, seed->c_str(), true);
            EVR_LOG("%s: before the profile's load %.*s %s -> %s, its value before the layer's first write",
                    kTag, static_cast<int>(c.name.size()), c.name.data(), live.c_str(), seed->c_str());
        }
    }
}

// Under heldMutex, after the load: the player's values, kept for the saves and for a trip; the one-time
// repair at the first load.
void afterLoad() {
    std::string list;
    for (ProfileCvar& c : g_cvars) {
        c.loaded = cvar_book::valueText(c.object);
        list += (list.empty() ? "" : ", ") + std::string(c.name) + " " + *c.loaded;
    }
    if (logCall()) {
        EVR_LOG("%s: the game loaded the profile: %s (the player's values, saved back as they are)", kTag,
                list.c_str());
    }
    if (!g_loadSeen) {
        g_loadSeen = true;
        ProfileCvar* shakes = named("view_skipShakes");
        g_repair = mp_policy::profileRepair(g_repairAsked, shakes ? shakes->loaded : std::nullopt);
        if (g_repair == mp_policy::ProfileRepair::Repair && shakes && !mp_guard::allowsGameTouch()) {
            EVR_LOG("%s: one-time repair skipped: the multiplayer guard tripped during the load; asked again "
                    "next session",
                    kTag);
        } else if (g_repair == mp_policy::ProfileRepair::Repair && shakes) {
            // A direct write, not cvar_book::write: a trip on another thread just after the check above
            // (restoreOnTrip runs under the book's lock, not heldMutex) can still be followed by this write.
            // The window is a few instructions and 0 is the game's default, so that is harmless.
            g_set(shakes->object, "0", true);
            shakes->loaded = cvar_book::valueText(shakes->object);
            g_repairWritten = true;
            EVR_LOG("%s: one-time repair (ETERNALVR_PROFILE_REPAIR=1): the profile's view_skipShakes 1, left "
                    "by a VR session before these hooks, set to %s (camera shake on, the game's default)",
                    kTag, shakes->loaded->c_str());
        } else if (g_repairAsked) {
            EVR_LOG("%s: one-time repair asked for; not needed (view_skipShakes %s)", kTag,
                    shakes && shakes->loaded ? shakes->loaded->c_str() : "not read");
        }
        reportRepair(false);
    }
    for (const ProfileCvar& c : g_cvars) {
        cvar_book::setTripValue(c.name, *c.loaded);
    }
}

// Under heldMutex, before the save: the player's values in.
std::vector<Swap> beforeSave(std::string& list) {
    std::vector<Swap> swapped;
    for (const ProfileCvar& c : g_cvars) {
        const std::string live = cvar_book::valueText(c.object);
        const mp_policy::ProfileWrite w =
            mp_policy::profileWrite(live, c.loaded, cvar_book::valueBefore(c.name));
        if (!w.swap) {
            continue;
        }
        g_set(c.object, w.value.c_str(), true);
        swapped.push_back(Swap{&c, live});
        list += (list.empty() ? "" : ", ") + std::string(c.name) + " " + w.value + " (held " + live + "; " +
                w.source + ")";
    }
    return swapped;
}

// Under heldMutex, after the save: the held values back, through the book (after a trip the player's value
// stays).
void afterSave(const std::vector<Swap>& swapped, const std::string& list, bool log) {
    for (const Swap& s : swapped) {
        cvar_book::write(s.cvar->name, s.cvar->object, g_set, s.held.c_str());
    }
    if (log) {
        const std::string what =
            list.empty() ? "no held value in it" : "the player's values went in: " + list;
        EVR_LOG("%s: the game saved the profile: %s", kTag, what.c_str());
    }
    if (g_repair == mp_policy::ProfileRepair::Repair && g_repairWritten) {
        EVR_LOG("%s: one-time repair saved: view_skipShakes 0 in the profile", kTag);
        reportRepair(true);
        g_repair = mp_policy::ProfileRepair::NotNeeded; // reported once; later saves are ordinary
    }
}

// The game's code never runs under heldMutex: the lock is held only to set up and to finish, and while the
// serializer runs, the profile I/O flag keeps apply() off these cvars (runtime_cvars::setProfileIo).
void onSerialize(void* settings, std::uint8_t* context, std::uintptr_t flag, std::uintptr_t r9) {
    if (!context || !mp_guard::allowsGameTouch()) {
        g_original(settings, context, flag, r9);
        return;
    }
    const bool save = *context != 0;
    std::vector<Swap> swapped;
    std::string list;
    bool log = false;
    ProfileIo io; // declared before the locks: on a throw it ends after they are released
    {
        std::lock_guard lock(runtime_cvars::heldMutex());
        io.begin();
        if (save) {
            log = logCall();
            swapped = beforeSave(list);
        } else {
            beforeLoad();
        }
    }
    g_original(settings, context, flag, r9);
    std::lock_guard lock(runtime_cvars::heldMutex());
    if (save) {
        afterSave(swapped, list, log);
    } else {
        afterLoad();
    }
    io.end();
}

// The one `mov rdx, [view_skipShakes]; test [rdx + 0x40], 0x30000` in the serializer's first bytes.
bool checksViewShakes(const GameImage& image, const std::byte* serializer, const std::byte* object) {
    const auto pattern = resolver::Pattern::parse(kArchiveCheck);
    if (!pattern || !object) {
        return false;
    }
    int found = 0;
    for (std::size_t i = 0; i < kArchiveCheckSpan; ++i) {
        const std::byte* at = serializer + i;
        if (!image.inText(at, pattern->size())) {
            break;
        }
        if (pattern->matchesAt(image.text, static_cast<std::size_t>(at - image.text.data())) &&
            ripTarget(image, at + 3, at + 7) == object) {
            ++found;
        }
    }
    return found == 1;
}

bool locate(const GameImage& image, const std::byte*& serializer) {
    serializer = findUnique(image, kTag, "profile cvar serializer", kSerializerSignature);
    g_set = reinterpret_cast<cvar_book::SetStringFn>(const_cast<std::byte*>(findCvarSetter(image)));
    const std::vector<std::string_view>& names = mp_policy::profileStoredCvars();
    const std::vector<std::byte*> objects = findCvarObjects(image, names);
    if (!serializer || !g_set) {
        return false;
    }
    for (std::size_t i = 0; i < names.size(); ++i) {
        if (!objects[i]) {
            return false;
        }
        g_cvars.push_back(ProfileCvar{names[i], objects[i], std::nullopt});
    }
    const ProfileCvar* shakes = named("view_skipShakes");
    if (!shakes || !checksViewShakes(image, serializer, shakes->object)) {
        EVR_LOG("%s: the serializer at RVA 0x%X does not write view_skipShakes as expected", kTag,
                image.rva(serializer));
        return false;
    }
    return true;
}

} // namespace

void installProfileHooks() {
    std::call_once(g_installOnce, [] {
        if (!(routeSRequested() || parallelEyesChangedEngine()) || !mp_guard::allowsGameTouch()) {
            return;
        }
        std::wstring repair;
        g_repairAsked = readEnv(L"ETERNALVR_PROFILE_REPAIR", repair) && repair == L"1";
        GameImage image;
        const std::byte* serializer = nullptr;
        if (!locateGameImage(image, kTag) || !locate(image, serializer)) {
            g_cvars.clear();
            EVR_LOG(
                "%s: not hooked; the game saves held values (view_skipShakes 1) into the player's profile",
                kTag);
            return;
        }
        std::string error;
        if (!installInlineHook(const_cast<std::byte*>(serializer), reinterpret_cast<void*>(&onSerialize),
                               reinterpret_cast<void**>(&g_original), error)) {
            g_cvars.clear();
            EVR_LOG("%s: serializer hook failed: %s; the game saves held values into the player's profile",
                    kTag, error.c_str());
            return;
        }
        EVR_LOG(
            "%s: profile cvar serializer hooked at RVA 0x%X: the profile keeps the player's view_skipShakes "
            "and r_sharpening while the layer holds them",
            kTag, image.rva(serializer));
    });
}

} // namespace evr::vkcore
