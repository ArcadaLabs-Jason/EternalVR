#include "vkcore/game_settings.hpp"

#include "vkcore/game_settings_line.hpp"
#include "vkcore/game_text.hpp"
#include "vkcore/log.hpp"
#include "vkcore/mp_guard.hpp"
#include "vkcore/runtime_cvars.hpp"
#include "vkcore/seh_filter.hpp"

#include <windows.h>

#include <atomic>
#include <cstddef>
#include <cstring>
#include <mutex>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace evr::vkcore::game_settings {

namespace {

// "game settings: " starts only the line itself, which the launcher's report looks for; everything else is
// logged under kCvarsTag.
constexpr const char* kTag = "game settings";
constexpr const char* kCvarsTag = "game settings cvars";
constexpr std::size_t kLongestName = 64;

std::mutex g_mutex;
LineSchedule g_schedule;
bool g_missingLogged = false;
// One per settingCvars() entry, nullptr when not found: written once by the locate thread, read by poll()
// only after g_locate is Done.
std::vector<const std::byte*> g_objects;

enum class Locate : int { NotStarted, Running, Done };
std::atomic<Locate> g_locate{Locate::NotStarted};

// Finds each cvar's object from the game's registrations (runtime_cvars.hpp): one scan of the code, the name
// of every registration compared with the list. A name registered more than once is left out.
void locate() {
    const std::span<const SettingCvar> cvars = settingCvars();
    g_objects.assign(cvars.size(), nullptr);
    GameImage image;
    const auto pattern = resolver::Pattern::parse(runtime_cvars::kRegistration);
    if (!pattern || !locateGameImage(image, kCvarsTag)) {
        EVR_LOG("%s: the game's code cannot be read; no line", kCvarsTag);
        return;
    }
    const ULONGLONG start = GetTickCount64();
    std::vector<int> found(cvars.size(), 0);
    for (const std::size_t offset : resolver::findAll(image.text, pattern.value())) {
        const std::byte* site = image.text.data() + offset;
        const std::byte* name = ripTarget(image, site + runtime_cvars::kRegistrationName + 3,
                                          site + runtime_cvars::kRegistrationName + 7);
        // Cvar names are string literals in .rdata (runtime_cvars finds them there); only those are read.
        if (!name || name < image.rdata.data() || name >= image.rdata.data() + image.rdata.size()) {
            continue;
        }
        const std::string_view text = stringAt(image, name, kLongestName);
        if (text.empty()) {
            continue;
        }
        for (std::size_t i = 0; i < cvars.size(); ++i) {
            if (text == cvars[i].name) {
                g_objects[i] = ripTarget(image, site + runtime_cvars::kRegistrationObject + 3,
                                         site + runtime_cvars::kRegistrationObject + 7);
                ++found[i];
            }
        }
    }
    std::size_t located = 0;
    for (std::size_t i = 0; i < cvars.size(); ++i) {
        if (found[i] > 1) {
            EVR_LOG("%s: %s registered %d times; left out", kCvarsTag, cvars[i].name, found[i]);
            g_objects[i] = nullptr;
        }
        located += g_objects[i] ? 1 : 0;
    }
    EVR_LOG("%s: %zu of %zu located (%llu ms)", kCvarsTag, located, cvars.size(),
            static_cast<unsigned long long>(GetTickCount64() - start));
}

// The values block's integer (+0x08) and float (+0x0C), as the engine keeps them; false instead of a crash
// when the cvar's memory cannot be read.
bool readCvar(const std::byte* object, int& integer, float& number) {
    __try {
        const std::byte* values = nullptr;
        std::memcpy(&values, object, sizeof(values));
        if (!values) {
            return false;
        }
        std::memcpy(&integer, values + 0x08, sizeof(integer));
        std::memcpy(&number, values + 0x0C, sizeof(number));
        return true;
    } __except (accessViolationOnly(GetExceptionCode())) {
        return false;
    }
}

} // namespace

void poll() {
    // Passive in multiplayer: before the guard arms and after a trip nothing in the game is read, not even
    // its code, and the schedule stands still.
    if (!mp_guard::allowsGameTouch()) {
        return;
    }
    std::unique_lock lock(g_mutex, std::try_to_lock);
    if (!lock.owns_lock()) {
        return;
    }
    // The scan of the game's code (about 15 ms) runs once on a thread of its own, not in the present hook;
    // the schedule waits for it.
    if (const Locate state = g_locate.load(std::memory_order_acquire); state != Locate::Done) {
        if (state == Locate::NotStarted) {
            g_locate.store(Locate::Running, std::memory_order_relaxed);
            std::thread([] {
                locate();
                g_locate.store(Locate::Done, std::memory_order_release);
            }).detach();
        }
        return;
    }
    if (!g_schedule.due(mp_guard::mapLoads(), GetTickCount64())) {
        return;
    }
    const std::span<const SettingCvar> cvars = settingCvars();
    std::vector<SettingValue> values;
    values.reserve(cvars.size());
    for (std::size_t i = 0; i < cvars.size(); ++i) {
        SettingValue v;
        v.name = cvars[i].name;
        v.kind = cvars[i].kind;
        if (i >= g_objects.size() || !g_objects[i]) {
            v.state = SettingValue::State::NotFound;
        } else if (!readCvar(g_objects[i], v.integer, v.number)) {
            v.state = SettingValue::State::Unreadable;
        }
        values.push_back(v);
    }
    if (!g_missingLogged) {
        g_missingLogged = true;
        const std::string missing = notFoundList(values);
        if (!missing.empty()) {
            EVR_LOG("%s: not registered once in this build, left out of the line: %s", kCvarsTag,
                    missing.c_str());
        }
    }
    const std::string line = formatLine(values);
    if (!line.empty() && g_schedule.shouldLog(line)) {
        EVR_LOG("%s: %s", kTag, line.c_str());
    }
}

} // namespace evr::vkcore::game_settings
