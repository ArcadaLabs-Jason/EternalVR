#include "vkcore/runtime_cvars.hpp"

#include "stereo_seq/seq_settings.hpp"
#include "vkcore/cvar_book.hpp"
#include "vkcore/game_text.hpp"
#include "vkcore/log.hpp"
#include "vkcore/mp_guard.hpp"
#include "vkcore/parallel_eyes_settings.hpp"
#include "vkcore/scatter_hooks.hpp"
#include "vkcore/taa_hooks.hpp"
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

constexpr const char* kTag = "cvars";
constexpr int kLoggedWrites = 12; // writes after each cvar's first: the first 12 logged, later ones counted

// idCVar::SetString (RVA 0x376020 in build 25216728): (cvar object, value, force).
constexpr const char* kSetStringSignature =
    "48 89 5C 24 10 48 89 74 24 18 57 48 83 EC 20 48 8B D9 48 8B 09 41 0F "
    "B6 F0 48 8B FA 48 85 D2 75 04 48 8B 79 30 48 8B 09 48 8B D7 E8";

using SetStringFn = cvar_book::SetStringFn;

struct Held {
    std::string name;
    std::string value;     // "?": only logged
    bool stereo = false;   // part of the Route S set (not ETERNALVR_DEBUG_CVARS)
    bool temporal = false; // a TAA cvar: left to the per-eye module once it is active
    bool saver = false;    // from ETERNALVR_CPU_SAVER
    bool scatter = false;  // r_lightScatteringTAA: follows the scattering history (scatterHold)
    bool cap = false;      // value "<=N": lowered to N while above it, never raised (capValue)
    float capValue = 0.0f;
    bool exact = false; // a float cvar held at exactValue, compared as a float (ETERNALVR_SHARPENING)
    float exactValue = 0.0f;
    bool written = false; // the first write is always logged
    std::byte* object = nullptr;
    bool logged = false;
    bool windowSize = false; // r_windowWidth / r_windowHeight of the window set: left alone with the render
                             // size off (windowSizeLeft)
    bool left = false;       // ... and that was logged
    bool placed = false;     // ... held at the window placed for a device without present scaling instead
    bool parallel = false;   // Parallel Eye Rendering's own set: an ETERNALVR_DEBUG_CVARS entry replaces it
};

std::mutex g_mutex;
bool g_started = false;
SetStringFn g_setString = nullptr;
std::vector<Held> g_held;
int g_loggedWrites = 0;
std::atomic<std::uint64_t> g_writes{0};
stereo_seq::StereoTemporal g_temporal = stereo_seq::StereoTemporal::Off;
bool g_parallelEyes = false;
bool g_parallelAntiAliasingOff = false;
bool g_parallelAntiAliasingHeld = true;

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
bool scatterHold(Held& h) {
    if (taaRequested() && !taaFailedClosed()) {
        return false;
    }
    h.value = std::string(stereo_seq::stereoScatterFilterCvar(scatterPerEyeReady()).value);
    return true;
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
        if (same && (same->saver || same->scatter || same->parallel)) {
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

// The window and present set (stereo_seq::stereoWindowCvars), as the command line and ETERNALVR_WINDOW size
// it; r_windowWidth / r_windowHeight follow the render size (windowSizeLeft).
void addWindowSet(bool parallel) {
    std::wstring window;
    readEnv(L"ETERNALVR_WINDOW", window);
    std::string narrowWindow;
    for (const wchar_t ch : window) {
        narrowWindow.push_back(ch < 0x80 ? static_cast<char>(ch) : '?');
    }
    for (const auto& c : stereo_seq::stereoWindowCvars(commandLine(), narrowWindow)) {
        g_held.push_back(Held{c.name, c.value, true, false});
        g_held.back().windowSize = stereo_seq::isWindowSizeCvar(c.name);
        g_held.back().parallel = parallel;
    }
}

// Parallel Eye Rendering's set, held from its first present (presenter_copy.cpp), on every frame:
// - r_useNewDepthDownscale 0. With the new depth downsample (0x1C73530) view 1's light binning got wrong tile
//   depth bounds in e1m3: its light lists drew tile-shaped black holes over the near floor (rig runs cum2,
//   cnd1); the old downsample (0x1C73780) leaves both views right.
// - Route S's window and present set, so a load path that applies the player's video mode cannot take the
//   eyes to the display's size (as in Route S).
// - Route S's comfort set (stereo_seq::stereoComfortCvars): without it the game's low-health damage view
//   effect left both eyes red after a death and checkpoint reload (rig runs pe1 and crt1), which Route
//   S never shows because it holds view_skipDamageEffect and the rest.
// - The launcher's anti-aliasing (parallel_eyes::antiAliasingCvars): TAA, or with Off the stereo path's
//   r_TAASafeMode 1 and r_antialiasing 0 (the game applies the player's own mode after the command line).
//   ETERNALVR_STEREO_RUNTIME_CVARS=0 leaves it as the game has it, as Route S's stereo set (rig experiments).
// An ETERNALVR_DEBUG_CVARS entry for one of these cvars wins; a CPU Saver item for one is left out, as in
// Route S.
void addParallelEyeSet() {
    g_held.push_back(Held{"r_useNewDepthDownscale", "0", true, false});
    g_held.back().parallel = true;
    if (g_parallelAntiAliasingHeld) {
        for (const auto& c : parallel_eyes::antiAliasingCvars(g_parallelAntiAliasingOff)) {
            g_held.push_back(Held{std::string(c.name), std::string(c.value), true, false});
            g_held.back().parallel = true;
        }
    } else {
        EVR_LOG("%s: Parallel Eye Rendering's anti-aliasing is left as the game has it "
                "(ETERNALVR_STEREO_RUNTIME_CVARS=0)",
                kTag);
    }
    addWindowSet(true);
    for (const auto& c : stereo_seq::stereoComfortCvars()) {
        g_held.push_back(Held{std::string(c.name), std::string(c.value), true, false});
        g_held.back().parallel = true;
    }
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
        addWindowSet(false);
        for (const auto& c : stereo_seq::stereoComfortCvars()) {
            g_held.push_back(Held{std::string(c.name), std::string(c.value), true, false});
        }
        const std::string ssdo = narrowEnv(L"ETERNALVR_STEREO_SSDO");
        if (const auto c = stereo_seq::stereoSsdoCvar(ssdo)) {
            g_held.push_back(Held{std::string(c->name), std::string(c->value), true, false});
        } else {
            EVR_LOG("%s: r_SSDO is left as the game has it (ETERNALVR_STEREO_SSDO=%s)", kTag, ssdo.c_str());
        }
    } else if (stereo) {
        EVR_LOG("%s: the stereo set is left as the game has it (ETERNALVR_STEREO_RUNTIME_CVARS=0)", kTag);
    }
    if (g_parallelEyes) {
        addParallelEyeSet(); // before the CPU Saver, as the stereo sets
    }
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
        if (h.object && h.scatter) {
            // Held only without per-eye TAA (scatterHold); 0 throughout when the scattering hooks are not in.
            list += (list.empty() ? "" : ", ") + h.name +
                    (scatterHooksInstalled() ? " 1 while the scattering history is per eye, else 0" : " 0") +
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

void apply(bool stereo) {
    std::lock_guard lock(g_mutex);
    if (!g_started) {
        g_started = true;
        start(stereo);
    }
    if (!g_setString || !mp_guard::allowsGameTouch()) {
        return;
    }
    for (Held& h : g_held) {
        if (!h.object || (h.temporal && g_temporal != stereo_seq::StereoTemporal::Off) ||
            (h.scatter && !scatterHold(h))) {
            continue;
        }
        holdPlacedWindow(h);
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
                    h.stereo  ? "; the game's setting is not changed"
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

void setParallelEyes(bool antiAliasingOff, bool antiAliasingHeld) {
    std::lock_guard lock(g_mutex);
    if (g_started) {
        EVR_LOG("%s: Parallel Eye set asked for after the first apply; not held", kTag);
        return;
    }
    g_parallelEyes = true;
    g_parallelAntiAliasingOff = antiAliasingOff;
    g_parallelAntiAliasingHeld = antiAliasingHeld;
}

} // namespace evr::vkcore::runtime_cvars
