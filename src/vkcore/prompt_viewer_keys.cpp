#include "vkcore/prompt_viewer_keys.hpp"

#include "vkcore/controllers_impl.hpp"
#include "vkcore/log.hpp"
#include "vkcore/mid_hook.hpp"
#include "vkcore/window_timing.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace evr::vkcore::controllers {

namespace {

constexpr std::string_view kViewerSprite = "v%d_viewer";
constexpr std::string_view kTitle = "title";
// How far into the update its KeyNameText calls (+0x148, +0x17B) and the title's read (+0x306) lie.
constexpr std::size_t kUpdateSpan = 0x400;
// Each call loads its key from the input platform (2) in ebx within 16 bytes before it: Q is 0x10, E 0x12.
constexpr std::array<const char*, 2> kKeyLoads{"8D 4B 0E", "8D 4B 10"};
constexpr std::size_t kLoadReach = 16;
// The title's text id, read after the "title" lea: `mov eax, [rdi+disp32]`.
constexpr const char* kTitleRead = "8B 87 ?? ?? 00 00";
constexpr std::size_t kTitleReach = 0x80;
constexpr std::int32_t kMaxField = 0x1000;
constexpr int kItemLines = 40;

std::int32_t g_titleField = 0;
std::atomic<std::uint64_t> g_lastUpdate{0}; // window_timing::nowMicros() of the update's last run
std::atomic<std::int32_t> g_lastTitle{0};
std::atomic<int> g_itemLines{0};
constexpr std::uint64_t kOpenGapMicros = 500000; // a gap this long between updates: the viewer was closed

// The update's start: rcx is the viewer. Runs on every frame the viewer is up.
void onViewerUpdate(const HookRegisters& r) {
    std::int32_t title = 0;
    if (!safeRead(reinterpret_cast<const std::byte*>(r.rcx) + g_titleField, title)) {
        return;
    }
    const std::uint64_t now = window_timing::nowMicros();
    const bool opened = now - g_lastUpdate.exchange(now, std::memory_order_relaxed) > kOpenGapMicros;
    const bool changed = g_lastTitle.exchange(title, std::memory_order_relaxed) != title;
    if ((opened || changed) && g_itemLines.fetch_add(1, std::memory_order_relaxed) < kItemLines) {
        EVR_LOG("prompts: the Fortress viewer %s item 0x%X (its title's text id)",
                opened ? "opens on" : "shows", static_cast<unsigned>(title));
    }
}

// The single function that loads "v%d_viewer" and calls KeyNameText twice, each after its key's load; nullptr
// otherwise. `returns` gets the calls' return addresses.
const std::byte* findViewerUpdate(const GameImage& image,
                                  const std::byte* keyNameText,
                                  std::array<std::uintptr_t, 2>& returns) {
    const std::byte* text = findUniqueString(image, kViewerSprite);
    const std::byte* found = nullptr;
    int count = 0;
    for (const std::byte* at :
         text != nullptr ? findLeaReferences(image, text) : std::vector<const std::byte*>{}) {
        const std::byte* function = functionStart(image, at);
        const std::vector<std::uintptr_t> calls = function != nullptr && function != found
                                                      ? callReturns(image, function, keyNameText, kUpdateSpan)
                                                      : std::vector<std::uintptr_t>{};
        if (calls.size() != returns.size()) {
            continue;
        }
        bool loads = true;
        for (std::size_t i = 0; i < calls.size(); ++i) {
            const std::byte* call = reinterpret_cast<const std::byte*>(calls[i]) - 5;
            bool loaded = false;
            for (std::size_t back = 3; back <= kLoadReach && !loaded; ++back) {
                loaded = matchesAt(image, call - back, kKeyLoads[i]);
            }
            loads = loads && loaded;
        }
        if (loads) {
            found = function;
            ++count;
            std::ranges::copy(calls, returns.begin());
        }
    }
    return count == 1 ? found : nullptr;
}

// The title's field: the `mov eax, [rdi+disp32]` closest after the update's "title" lea, or 0.
std::int32_t findTitleField(const GameImage& image, const std::byte* update) {
    const std::byte* title = findUniqueString(image, kTitle);
    for (const std::byte* at :
         title != nullptr ? findLeaReferences(image, title) : std::vector<const std::byte*>{}) {
        if (at < update || at >= update + kUpdateSpan || functionStart(image, at) != update) {
            continue;
        }
        for (std::size_t i = 7; i < kTitleReach; ++i) {
            if (matchesAt(image, at + i, kTitleRead)) {
                const std::int32_t field = readI32(at + i + 2);
                return field > 0 && field < kMaxField ? field : 0;
            }
        }
    }
    return 0;
}

} // namespace

std::array<std::uintptr_t, 2>
installViewerKeyHooks(const GameImage& image, const std::byte* keyNameText, const char* tag) {
    std::array<std::uintptr_t, 2> returns{};
    const std::byte* update = findViewerUpdate(image, keyNameText, returns);
    if (update == nullptr) {
        EVR_LOG(
            "%s: Fortress viewer: no single update that loads \"v%%d_viewer\" with its Q / E key texts; its "
            "keys stay the game's",
            tag);
        return {};
    }
    EVR_LOG("%s: Fortress viewer: update at RVA 0x%X, its key texts at 0x%X, 0x%X (the grips' names)", tag,
            image.rva(update), image.rva(reinterpret_cast<const std::byte*>(returns[0]) - 5),
            image.rva(reinterpret_cast<const std::byte*>(returns[1]) - 5));
    g_titleField = findTitleField(image, update);
    std::string error;
    if (g_titleField == 0) {
        EVR_LOG("%s: Fortress viewer: no title read in its update; the item shown is not logged", tag);
    } else if (!installMidHook(const_cast<std::byte*>(update), &onViewerUpdate, error)) {
        EVR_LOG("%s: Fortress viewer: update hook at RVA 0x%X failed: %s; the item shown is not logged", tag,
                image.rva(update), error.c_str());
    }
    return returns;
}

} // namespace evr::vkcore::controllers
