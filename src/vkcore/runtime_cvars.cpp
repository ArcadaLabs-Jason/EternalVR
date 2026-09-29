#include "vkcore/runtime_cvars.hpp"

#include "stereo_seq/seq_settings.hpp"
#include "vkcore/game_text.hpp"
#include "vkcore/log.hpp"
#include "vkcore/mp_guard.hpp"

#include <windows.h>

#include <atomic>
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
// A cvar registration: lea r8, [default]; lea rdx, [name]; lea rcx, [object]; call.
constexpr const char* kRegistration = "4C 8D 05 ?? ?? ?? ?? 48 8D 15 ?? ?? ?? ?? 48 8D 0D ?? ?? ?? ?? E8";
constexpr std::size_t kRegistrationName = 7;
constexpr std::size_t kRegistrationObject = 14;

using SetStringFn = void (*)(void* cvar, const char* value, bool force);

struct Held {
    std::string name;
    std::string value;     // "?": only logged
    bool stereo = false;   // part of the Route S set (not ETERNALVR_DEBUG_CVARS)
    bool temporal = false; // a TAA cvar: left to the per-eye module once it is active
    bool saver = false;    // from ETERNALVR_CPU_SAVER
    bool cap = false;      // value "<=N": lowered to N while above it, never raised (capValue)
    float capValue = 0.0f;
    bool written = false; // the first write is always logged
    std::byte* object = nullptr;
    bool logged = false;
};

std::mutex g_mutex;
bool g_started = false;
SetStringFn g_setString = nullptr;
std::vector<Held> g_held;
int g_loggedWrites = 0;
std::atomic<std::uint64_t> g_writes{0};
stereo_seq::StereoTemporal g_temporal = stereo_seq::StereoTemporal::Off;

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

// ETERNALVR_CPU_SAVER="name=value;..." (the launcher's processor saver, data/cpu-saver.txt): cvars that cut
// the CPU work of a render, held like the others. A cvar the stereo sets already hold keeps their value.
void addCpuSaver() {
    const std::string text = narrowEnv(L"ETERNALVR_CPU_SAVER");
    if (text.empty() || text == "0") {
        return;
    }
    std::string list;
    for (const stereo_seq::CvarHold& c : stereo_seq::parseCvarList(text)) {
        Held h;
        if (c.value.empty() || heldNamed(c.name) || !makeHeld(c, h)) {
            EVR_LOG("%s: processor saver: %s left out (%s)", kTag, c.name.c_str(),
                    heldNamed(c.name) ? "the stereo set holds it" : "no usable value");
            continue;
        }
        h.saver = true;
        g_held.push_back(std::move(h));
        list += (list.empty() ? "" : ", ") + c.name + " " + c.value;
    }
    EVR_LOG("%s: processor saver (ETERNALVR_CPU_SAVER) asks for: %s", kTag,
            list.empty() ? "nothing (no name=value item)" : list.c_str());
}

// ETERNALVR_DEBUG_CVARS: rig experiments; an entry replaces the processor saver's value for the same cvar.
void addDebugList() {
    for (const stereo_seq::CvarHold& c : stereo_seq::parseCvarList(narrowEnv(L"ETERNALVR_DEBUG_CVARS"))) {
        Held h;
        if (!makeHeld(c, h)) {
            EVR_LOG("%s: %s=%s is not a usable value; left alone", kTag, c.name.c_str(), c.value.c_str());
            continue;
        }
        Held* same = heldNamed(c.name);
        if (same && same->saver) {
            *same = std::move(h);
        } else {
            g_held.push_back(std::move(h));
        }
    }
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
        std::wstring window;
        readEnv(L"ETERNALVR_WINDOW", window);
        std::string narrowWindow;
        for (const wchar_t ch : window) {
            narrowWindow.push_back(ch < 0x80 ? static_cast<char>(ch) : '?');
        }
        for (const auto& c : stereo_seq::stereoWindowCvars(commandLine(), narrowWindow)) {
            g_held.push_back(Held{c.name, c.value, true, false});
        }
        for (const auto& c : stereo_seq::stereoComfortCvars()) {
            g_held.push_back(Held{std::string(c.name), std::string(c.value), true, false});
        }
    } else if (stereo) {
        EVR_LOG("%s: the stereo set is left as the game has it (ETERNALVR_STEREO_RUNTIME_CVARS=0)", kTag);
    }
    addCpuSaver();
    addDebugList();
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
    for (const Held& h : g_held) {
        if (h.object) {
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
        EVR_LOG("%s: processor saver holds: %s", kTag, saver.empty() ? "none" : saver.c_str());
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
        if (!h.object || (h.temporal && g_temporal != stereo_seq::StereoTemporal::Off)) {
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
            g_setString(h.object, h.value.c_str(), true);
            g_writes.fetch_add(1, std::memory_order_relaxed);
            if (!h.written || g_loggedWrites < kLoggedWrites) {
                g_loggedWrites += h.written ? 1 : 0;
                h.written = true;
                EVR_LOG("%s: %s %.3f -> at most %s (reads %.3f)%s", kTag, h.name.c_str(), value,
                        h.value.c_str(), readFloat(h.object), h.saver ? "; processor saver" : "");
            }
            continue;
        }
        if (before == std::atoi(h.value.c_str())) {
            continue;
        }
        g_setString(h.object, h.value.c_str(), true);
        g_writes.fetch_add(1, std::memory_order_relaxed);
        if (!h.written || g_loggedWrites < kLoggedWrites) {
            g_loggedWrites += h.written ? 1 : 0;
            h.written = true;
            EVR_LOG("%s: %s %d -> %s (reads %d)%s", kTag, h.name.c_str(), before, h.value.c_str(),
                    readValue(h.object),
                    h.stereo  ? "; the game's setting is not changed"
                    : h.saver ? "; processor saver"
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

} // namespace evr::vkcore::runtime_cvars
