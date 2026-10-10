#include "vkcore/runtime_cvars.hpp"
#include "vkcore/runtime_cvars_impl.hpp"

#include "stereo_seq/seq_settings.hpp"
#include "stereo_seq/setting_follow.hpp"
#include "vkcore/cvar_book.hpp"
#include "vkcore/game_text.hpp"
#include "vkcore/log.hpp"
#include "vkcore/mp_guard.hpp"
#include "vkcore/scatter_hooks.hpp"
#include "vkcore/ssdo_hooks.hpp"
#include "vkcore/ssdo_menu_hook.hpp"
#include "vkcore/status_file.hpp"
#include "vkcore/taa_hooks.hpp"
#include "vkcore/taa_ssr.hpp"
#include "vkcore/view_dlss.hpp"
#include "vkcore/virtual_client.hpp"
#include "vkcore/window_cap.hpp"

#include <windows.h>

#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace evr::vkcore::runtime_cvars {

namespace {

constexpr int kLoggedWrites = 12; // writes after each cvar's first: the first 12 logged, later ones counted

// idCVar::SetString (RVA 0x376020 in build 25216728): (cvar object, value, force).
constexpr const char* kSetStringSignature =
    "48 89 5C 24 10 48 89 74 24 18 57 48 83 EC 20 48 8B D9 48 8B 09 41 0F "
    "B6 F0 48 8B FA 48 85 D2 75 04 48 8B 79 30 48 8B 09 48 8B D7 E8";

using SetStringFn = cvar_book::SetStringFn;

std::mutex g_mutex;
bool g_started = false;
SetStringFn g_setString = nullptr;
std::vector<Held> g_held;
int g_loggedWrites = 0;
std::atomic<std::uint64_t> g_writes{0};
stereo_seq::StereoTemporal g_temporal = stereo_seq::StereoTemporal::Off;

// The window set's size is left to the game once the render size is off: the game then renders at its
// window's size, and holding the launch size (the render size) makes the game resize its window, which an
// AMD driver answers with VK_ERROR_OUT_OF_DATE_KHR on the next present. With the render size on (or not
// wanted) the size stays held. On a device without present scaling the window was placed at the largest
// eye-shaped size its display allows (window_cap.hpp): the size is held at that window's, which the game
// already reads, so the window keeps its size.
bool windowSizeLeft(const Held& h) {
    return h.windowSize && virtual_client::sizeOff() && !window_cap::placedSize();
}

void holdPlacedWindow(Held& h) {
    const auto placed = window_cap::placedSize();
    if (!h.windowSize || h.placed || !placed) {
        return;
    }
    h.placed = true;
    const bool width = h.name.size() >= 5 && _stricmp(h.name.c_str() + h.name.size() - 5, "width") == 0;
    const std::string value = std::to_string(width ? placed->width : placed->height);
    EVR_LOG("%s: %s is held at the placed window's %s, not %s (no present scaling)", kTag, h.name.c_str(),
            value.c_str(), h.value.c_str());
    h.value = value;
}

// The scattering filter follows its per-eye history, whatever the TAA mode
// (stereo_seq::stereoScatterFilterCvar): held here with per-eye TAA off or failed closed; while per-eye TAA
// is requested and has not failed closed, its own set writes the same value. False: left alone now.
// SSDO's filter (h.ssdo) by the same rule (stereo_seq::stereoSsdoFilterCvar).
bool historyFilterHold(Held& h) {
    if (taaRequested() && !taaFailedClosed()) {
        return false;
    }
    h.value = std::string(h.ssdo ? stereo_seq::stereoSsdoFilterCvar(ssdoPerEyeReady()).value
                                 : stereo_seq::stereoScatterFilterCvar(scatterPerEyeReady()).value);
    return true;
}

// r_SSDO follows the game's Directional Occlusion setting once it has run (ssdo_menu_hook.hpp: the profile's
// load, an overall preset or the video menu's apply); the knock-on's r_SSDO 0 is still written over.
void followSsdoSetting(Held& h) {
    const std::string_view value = stereo_seq::ssdoHoldValue(h.value, ssdoMenuChoice());
    if (value == h.value) {
        return;
    }
    h.value = std::string(value);
    EVR_LOG("%s: r_SSDO held at %s from now on (the game's Directional Occlusion setting)", kTag,
            h.value.c_str());
}

// The status file's ssr_follow and ssdo_follow (status_file.hpp): 1 while the hold is at the player's own
// setting (the game's setting has run) with per-eye TAA on, so r_TAASafeMode is 0 and the knock-on's 0 is not
// what the game saves; 0 otherwise (before the first stereo tick, failed closed, after a multiplayer guard
// trip, the setter not found). With 1, ssr_value and ssdo_value are the value held. The launcher's restore
// keeps the r_SSR or r_SSDO the game saved only after 1, and only when the game saved that value.
void reportFollows() {
    static int ssrReported = -2; // the value last written (stereo_seq::followFields), -2 before the first
    static int ssdoReported = -2;
    const bool perEye = taaPerEyeActive();
    const int ssr = perEye ? ssrFollowedValue() : -1;
    bool ssdoHeld = false;
    for (const Held& h : g_held) {
        ssdoHeld = ssdoHeld || (h.menuSsdo && h.object);
    }
    const int ssdo = perEye && ssdoHeld ? ssdoMenuChoice() : -1; // the hold's value (followSsdoSetting)
    for (const auto& [key, value] : stereo_seq::followFields("ssr", ssr, ssrReported)) {
        status::field(key.c_str(), value);
    }
    for (const auto& [key, value] : stereo_seq::followFields("ssdo", ssdo, ssdoReported)) {
        status::field(key.c_str(), value);
    }
    ssrReported = ssr;
    ssdoReported = ssdo;
}

// The cvar's integer value as the engine keeps it (the values block, +0x08).
int readValue(const std::byte* object) {
    const std::byte* values = nullptr;
    std::memcpy(&values, object, sizeof(values));
    int number = 0;
    if (values) {
        std::memcpy(&number, values + 8, sizeof(number));
    }
    return number;
}

// The cvar's float value (the values block, +0x0C; the renderer reads it there, e.g. 0x1CFC7C3).
float readFloat(const std::byte* object) {
    const std::byte* values = nullptr;
    std::memcpy(&values, object, sizeof(values));
    float number = 0.0f;
    if (values) {
        std::memcpy(&number, values + 0x0C, sizeof(number));
    }
    return number;
}

// A held entry; a "<=N" value becomes a cap. False for a cap whose number does not parse.
bool makeHeld(const stereo_seq::CvarHold& c, Held& out) {
    out = Held{c.name, c.value};
    if (c.value.rfind("<=", 0) == 0) {
        const std::optional<float> cap = stereo_seq::parseCvarCap(c.value);
        if (!cap) {
            return false;
        }
        out.cap = true;
        out.capValue = *cap;
        out.value = c.value.substr(2);
    }
    return true;
}

Held* heldNamed(std::string_view name) {
    for (Held& h : g_held) {
        if (h.name.size() == name.size() && _strnicmp(h.name.c_str(), name.data(), name.size()) == 0) {
            return &h;
        }
    }
    return nullptr;
}

// ETERNALVR_CPU_SAVER="name=value;..." (the launcher's CPU Saver, data/cpu-saver.txt): cvars that cut
// the CPU work of a render, held like the others. A cvar the stereo sets or Parallel Eye Rendering's set
// already hold keeps their value.
void addCpuSaver() {
    const std::string text = narrowEnv(L"ETERNALVR_CPU_SAVER");
    if (text.empty() || text == "0") {
        return;
    }
    std::string list;
    for (const stereo_seq::CvarHold& c : stereo_seq::parseCvarList(text)) {
        Held h;
        if (c.value.empty() || heldNamed(c.name) || !makeHeld(c, h)) {
            EVR_LOG("%s: CPU Saver: %s left out (%s)", kTag, c.name.c_str(),
                    heldNamed(c.name) ? "the stereo set holds it" : "no usable value");
            continue;
        }
        h.saver = true;
        g_held.push_back(std::move(h));
        list += (list.empty() ? "" : ", ") + c.name + " " + c.value;
    }
    EVR_LOG("%s: CPU Saver (ETERNALVR_CPU_SAVER) asks for: %s", kTag,
            list.empty() ? "nothing (no name=value item)" : list.c_str());
}

// ETERNALVR_SHARPENING=<number> (the launcher's Sharpening): r_sharpening held at that strength. Its value in
// the game's menu (a fraction such as 1.99) is compared as a float, so a hold of 1 is not taken for 1.99.
void addSharpening() {
    const std::string text = narrowEnv(L"ETERNALVR_SHARPENING");
    if (text.empty()) {
        return;
    }
    char* end = nullptr;
    const float value = std::strtof(text.c_str(), &end);
    if (end == text.c_str() || *end != '\0' || !(value >= 0.0f && value <= 10.0f)) {
        EVR_LOG("%s: ETERNALVR_SHARPENING=%s is not a number from 0 to 10; the game's sharpening stays", kTag,
                text.c_str());
        return;
    }
    Held h{"r_sharpening", text};
    h.exact = true;
    h.exactValue = value;
    g_held.push_back(std::move(h));
    EVR_LOG("%s: Sharpening (ETERNALVR_SHARPENING) asks for r_sharpening %s", kTag, text.c_str());
}

// ETERNALVR_DEBUG_CVARS: rig experiments; an entry replaces the CPU Saver's value for the same cvar.
void addDebugList() {
    for (const stereo_seq::CvarHold& c : stereo_seq::parseCvarList(narrowEnv(L"ETERNALVR_DEBUG_CVARS"))) {
        Held h;
        if (!makeHeld(c, h)) {
            EVR_LOG("%s: %s=%s is not a usable value; left alone", kTag, c.name.c_str(), c.value.c_str());
            continue;
        }
        Held* same = heldNamed(c.name);
        if (same && (same->saver || same->scatter || same->ssdo || same->parallel)) {
            *same = std::move(h);
        } else {
            g_held.push_back(std::move(h));
        }
    }
}

// The game's prompts stay in their keyboard form, which prompt_hooks.cpp renames to the VR buttons: a gamepad
// or Steam Input would switch them to pad glyphs naming the pad's binds. ETERNALVR_BUTTON_PROMPTS=0 leaves
// the cvar as the game has it, and an ETERNALVR_DEBUG_CVARS entry for it wins.
void addPromptPlatform() {
    if (narrowEnv(L"ETERNALVR_BUTTON_PROMPTS") == "0" || heldNamed("swf_platformOverride")) {
        return;
    }
    Held h;
    h.name = "swf_platformOverride";
    h.value = "2";
    g_held.push_back(std::move(h));
}

// The game's command line as UTF-8.
std::string commandLine() {
    const wchar_t* wide = GetCommandLineW();
    const int bytes = WideCharToMultiByte(CP_UTF8, 0, wide, -1, nullptr, 0, nullptr, nullptr);
    if (bytes <= 1) {
        return {};
    }
    std::string line(static_cast<std::size_t>(bytes), '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide, -1, line.data(), bytes, nullptr, nullptr);
    line.resize(static_cast<std::size_t>(bytes - 1));
    return line;
}

void start(bool stereo) {
    std::wstring off;
    if (stereo && !(readEnv(L"ETERNALVR_STEREO_RUNTIME_CVARS", off) && off == L"0")) {
        for (const auto& c : stereo_seq::stereoRuntimeCvars(stereo_seq::StereoTemporal::Off)) {
            g_held.push_back(Held{std::string(c.name), std::string(c.value), true, true});
        }
        const stereo_seq::CvarExpectation scatter = stereo_seq::stereoScatterFilterCvar(false);
        g_held.push_back(Held{std::string(scatter.name), std::string(scatter.value), true, false});
        g_held.back().scatter = true;
        const stereo_seq::CvarExpectation ssdoFilter = stereo_seq::stereoSsdoFilterCvar(false);
        g_held.push_back(Held{std::string(ssdoFilter.name), std::string(ssdoFilter.value), true, false});
        g_held.back().ssdo = true;
        addWindowSet(g_held, false);
        for (const auto& c : stereo_seq::stereoComfortCvars()) {
            g_held.push_back(comfortHeld(c));
        }
        const std::string ssdo = narrowEnv(L"ETERNALVR_STEREO_SSDO");
        if (const auto c = stereo_seq::stereoSsdoCvar(ssdo)) {
            g_held.push_back(Held{std::string(c->name), std::string(c->value), true, false});
            g_held.back().menuSsdo = true;
        } else {
            EVR_LOG("%s: r_SSDO is left as the game has it (ETERNALVR_STEREO_SSDO=%s)", kTag, ssdo.c_str());
        }
    } else if (stereo) {
        EVR_LOG("%s: the stereo set is left as the game has it (ETERNALVR_STEREO_RUNTIME_CVARS=0)", kTag);
    }
    addParallelEyeSet(g_held); // before the CPU Saver, as the stereo sets
    addCpuSaver();
    addSharpening();
    addDebugList();
    addPromptPlatform();
    GameImage image;
    if (g_held.empty() || !locateGameImage(image, kTag)) {
        return;
    }
    g_setString = reinterpret_cast<SetStringFn>(
        const_cast<std::byte*>(findUnique(image, kTag, "cvar SetString", kSetStringSignature)));
    auto pattern = resolver::Pattern::parse(kRegistration);
    if (!g_setString || !pattern) {
        EVR_LOG("%s: no cvar setter; the cvars stay as the game has them", kTag);
        g_setString = nullptr;
        return;
    }
    // The trip gives back what the layer writes (cvar_book.hpp); registered now so a trip logs it either way.
    cvar_book::listen();
    std::vector<const std::byte*> names;
    std::vector<int> found(g_held.size(), 0);
    for (const Held& h : g_held) {
        names.push_back(findUniqueString(image, h.name));
    }
    for (const std::size_t offset : resolver::findAll(image.text, pattern.value())) {
        const std::byte* site = image.text.data() + offset;
        const std::byte* name = ripTarget(image, site + kRegistrationName + 3, site + kRegistrationName + 7);
        for (std::size_t i = 0; i < g_held.size(); ++i) {
            if (name && name == names[i]) {
                g_held[i].object = const_cast<std::byte*>(
                    ripTarget(image, site + kRegistrationObject + 3, site + kRegistrationObject + 7));
                ++found[i];
            }
        }
    }
    for (std::size_t i = 0; i < g_held.size(); ++i) {
        if (found[i] != 1) {
            EVR_LOG("%s: %s registered %d time(s); left alone", kTag, g_held[i].name.c_str(), found[i]);
            g_held[i].object = nullptr;
        }
    }
    std::string list;
    std::string saver;
    bool anySaver = false;
    for (Held& h : g_held) {
        holdPlacedWindow(h);
        if (h.object && (h.scatter || h.ssdo)) {
            // Held only without per-eye TAA (historyFilterHold); 0 throughout when the history's hooks are
            // not in.
            const bool hooked = h.ssdo ? ssdoHooksInstalled() : scatterHooksInstalled();
            list += (list.empty() ? "" : ", ") + h.name +
                    (!hooked  ? " 0"
                     : h.ssdo ? " 1 while the SSDO history is per eye, else 0"
                              : " 1 while the scattering history is per eye, else 0") +
                    (taaRequested() ? " (if per-eye TAA fails closed)" : "");
        } else if (h.object && !windowSizeLeft(h)) {
            list += (list.empty() ? "" : ", ") + h.name + (h.cap ? " at most " : " ") + h.value;
        }
        if (h.saver) {
            anySaver = true;
            if (h.object) {
                saver += (saver.empty() ? "" : ", ") + h.name + (h.cap ? " at most " : " ") + h.value;
            }
        }
    }
    EVR_LOG("%s: held at run time: %s", kTag, list.empty() ? "none" : list.c_str());
    if (anySaver) {
        EVR_LOG("%s: CPU Saver holds: %s", kTag, saver.empty() ? "none" : saver.c_str());
    }
}

} // namespace

std::mutex& heldMutex() {
    return g_mutex;
}

bool applied() {
    return g_started;
}

std::string narrowEnv(const wchar_t* name) {
    std::wstring text;
    if (!readEnv(name, text)) {
        return {};
    }
    std::string narrow;
    for (const wchar_t c : text) {
        narrow.push_back(c < 0x80 ? static_cast<char>(c) : '?'); // cvar names and values are ASCII
    }
    return narrow;
}

Held comfortHeld(const stereo_seq::CvarExpectation& c) {
    Held h{std::string(c.name), std::string(c.value), true, false};
    if (c.fraction) {
        h.exact = true;
        h.exactValue = std::strtof(h.value.c_str(), nullptr);
    }
    return h;
}

void addWindowSet(std::vector<Held>& held, bool parallel) {
    std::wstring window;
    readEnv(L"ETERNALVR_WINDOW", window);
    std::string narrowWindow;
    for (const wchar_t ch : window) {
        narrowWindow.push_back(ch < 0x80 ? static_cast<char>(ch) : '?');
    }
    for (const auto& c : stereo_seq::stereoWindowCvars(commandLine(), narrowWindow)) {
        held.push_back(Held{c.name, c.value, true, false});
        held.back().windowSize = stereo_seq::isWindowSizeCvar(c.name);
        held.back().parallel = parallel;
    }
}

void apply(bool stereo) {
    std::lock_guard lock(g_mutex);
    if (!g_started) {
        g_started = true;
        start(stereo);
    }
    if (stereo) {
        reportFollows();
    }
    if (!g_setString || !mp_guard::allowsGameTouch()) {
        return;
    }
    for (Held& h : g_held) {
        if (!h.object || (h.temporal && g_temporal != stereo_seq::StereoTemporal::Off) ||
            ((h.scatter || h.ssdo) && !historyFilterHold(h)) ||
            (h.viewDlss && !viewDlssHolds(h.name, h.value))) {
            continue;
        }
        holdPlacedWindow(h);
        if (h.menuSsdo) {
            followSsdoSetting(h);
        }
        if (windowSizeLeft(h)) {
            if (!h.left) {
                h.left = true;
                EVR_LOG(
                    "%s: %s is left at the game's %d, not held at %s: the render size is off, so the game "
                    "renders at its window's size (a held size would resize the window)",
                    kTag, h.name.c_str(), readValue(h.object), h.value.c_str());
            }
            continue;
        }
        const int before = readValue(h.object);
        if (h.value == "?") {
            if (!h.logged) {
                h.logged = true;
                EVR_LOG("%s: %s reads %d", kTag, h.name.c_str(), before);
            }
            continue;
        }
        if (h.cap) {
            // Only lowered: a player on a lower quality level keeps it (a small margin for the float's
            // rounding).
            const float value = readFloat(h.object);
            if (!(value > h.capValue + 1e-4f)) {
                continue;
            }
            if (!cvar_book::write(h.name, h.object, g_setString, h.value.c_str())) {
                continue;
            }
            g_writes.fetch_add(1, std::memory_order_relaxed);
            if (!h.written || g_loggedWrites < kLoggedWrites) {
                g_loggedWrites += h.written ? 1 : 0;
                h.written = true;
                EVR_LOG("%s: %s %.3f -> at most %s (reads %.3f)%s", kTag, h.name.c_str(), value,
                        h.value.c_str(), readFloat(h.object), h.saver ? "; CPU Saver" : "");
            }
            continue;
        }
        if (h.exact) {
            const float value = readFloat(h.object);
            if (std::fabs(value - h.exactValue) < 1e-4f) {
                continue;
            }
            if (!cvar_book::write(h.name, h.object, g_setString, h.value.c_str())) {
                continue;
            }
            g_writes.fetch_add(1, std::memory_order_relaxed);
            if (!h.written || g_loggedWrites < kLoggedWrites) {
                g_loggedWrites += h.written ? 1 : 0;
                h.written = true;
                EVR_LOG("%s: %s %.3f -> %s (reads %.3f)", kTag, h.name.c_str(), value, h.value.c_str(),
                        readFloat(h.object));
            }
            continue;
        }
        if (before == std::atoi(h.value.c_str())) {
            continue;
        }
        if (!cvar_book::write(h.name, h.object, g_setString, h.value.c_str())) {
            continue;
        }
        g_writes.fetch_add(1, std::memory_order_relaxed);
        if (!h.written || g_loggedWrites < kLoggedWrites) {
            g_loggedWrites += h.written ? 1 : 0;
            h.written = true;
            EVR_LOG("%s: %s %d -> %s (reads %d)%s", kTag, h.name.c_str(), before, h.value.c_str(),
                    readValue(h.object),
                    h.stereo  ? "; held in VR"
                    : h.saver ? "; CPU Saver"
                              : "");
        }
    }
}

void setStereoTemporal(stereo_seq::StereoTemporal temporal) {
    std::lock_guard lock(g_mutex);
    if (temporal != g_temporal) {
        EVR_LOG("%s: stereo temporal effects %s; the TAA cvars are %s", kTag,
                temporal == stereo_seq::StereoTemporal::PerEye ? "per eye" : "off",
                temporal == stereo_seq::StereoTemporal::PerEye ? "left to the per-eye module" : "held off");
    }
    g_temporal = temporal;
}

std::uint64_t writes() {
    return g_writes.load(std::memory_order_relaxed);
}

bool holds(std::string_view name, std::string_view value) {
    std::unique_lock lock(g_mutex, std::try_to_lock); // apply() may be in the engine's setter
    const Held* h = lock.owns_lock() ? heldNamed(name) : nullptr;
    return h && h->object && !h->cap && !h->exact && h->value == value;
}

} // namespace evr::vkcore::runtime_cvars
