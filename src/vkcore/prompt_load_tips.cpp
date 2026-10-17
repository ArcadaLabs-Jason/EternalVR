#include "vkcore/prompt_load_tips.hpp"

#include "features/input/prompt_keys.hpp"
#include "features/menu/load_tips_keys.hpp"
#include "vkcore/controllers_impl.hpp"
#include "vkcore/keep_active.hpp"
#include "vkcore/key_inject.hpp"
#include "vkcore/log.hpp"
#include "vkcore/mid_hook.hpp"
#include "vkcore/mp_guard.hpp"
#include "vkcore/stall_watch.hpp"
#include "vkcore/window_timing.hpp"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace evr::vkcore::controllers {

namespace {

constexpr std::string_view kPreviousLabel = "#str_code_loadscreen_previous_GHOST47115";
constexpr std::string_view kNextLabel = "#str_code_loadscreen_next_GHOST47116";
constexpr std::string_view kKeyTokenFormat = "K_%s %s";
// The bar's half with the button's name in place of the key token, as the text pass would show a key.
constexpr const char* kButtonFormat = "[%s] %s";
// How far past its label's string each half's format call lies (0x40 bytes in both builds).
constexpr std::size_t kFormatReach = 0x80;
constexpr const char* kFormatCall = "4C 8B C0 E8"; // mov r8, rax; call
// The bar's poll of the pad's left shoulder (key 0x104) right after Q's; the update spans 0x2000 bytes.
constexpr const char* kShoulderPoll = "41 B8 04 01 00 00";
constexpr const char* kNextShoulderPoll = "41 B8 05 01 00 00";
constexpr std::size_t kUpdateSpan = 0x2400;
// The bar is written again when the input platform differs from the one it was written for: `cmp eax,
// [r13+disp32]; je` (+0x544 in both builds, r13 the loading screen). Hooked at that compare, so the base
// register is the one the compare itself reads.
constexpr const char* kPlatformCompare = "41 3B 85 ?? ?? 00 00 0F 84";
constexpr std::int32_t kMaxPlatformField = 0x1000;
// The log's gap line from the bar's last poll to the game's first view after it.
constexpr double kGapLogSeconds = 10.0; // longer: not the end of that loading screen
constexpr std::size_t kKeyNameReach = 16;

GameImage g_image;
std::uintptr_t g_keyTokenFormat = 0;      // "K_%s %s" in .rdata
input::KeyNameCallers g_callers;          // only loadTips is set
std::atomic<std::uint64_t> g_polledAt{0}; // window_timing::nowMicros() of the bar's last poll, 0: none yet
std::atomic<bool> g_pollInstalled{false}; // the poll hook is in: the grips may send the keys
std::array<std::atomic<bool>, 2> g_textLogged{};
std::atomic<bool> g_pollLogged{false};
std::int32_t g_platformField = 0; // the loading screen's field of the platform its bar was written for
std::atomic<bool> g_writtenUnnamed{false}; // the bar was written before the controllers' names were known
menu::LoadTipsKeys g_keys;                 // XR worker only
int g_keyLines = 0;                        // XR worker only
std::uint64_t g_gapLoggedFor = 0;          // XR worker only: the last poll whose gap to play was logged
constexpr int kKeyLines = 20;

// At the platform compare: a bar written before the controllers' names were known (the loading screen of a
// game started straight into a map) is written again once they are. The platform it was written for is set
// to none just before the compare reads it through r13.
void onPlatformCompare(const HookRegisters& r) {
    if (g_platformField == 0 || !g_writtenUnnamed.load(std::memory_order_relaxed) || !currentPromptLabels() ||
        !mp_guard::allowsGameTouch()) {
        return;
    }
    constexpr std::int32_t kNoPlatform = -1;
    auto* field = reinterpret_cast<std::byte*>(r.r13) + g_platformField;
    if (safeCopy(field, &kNoPlatform, sizeof(kNoPlatform))) {
        g_writtenUnnamed.store(false, std::memory_order_relaxed);
        EVR_LOG("prompts: loading screen tips: the bar was written before the controllers' names were known; "
                "written again");
    }
}

void onShoulderPoll(const HookRegisters&) {
    g_polledAt.store(window_timing::nowMicros(), std::memory_order_relaxed);
    if (!g_pollLogged.exchange(true, std::memory_order_relaxed)) {
        EVR_LOG(
            "prompts: the loading screen's tips bar polls its keys: the left grip sends Q (previous), the "
            "right grip E (next)");
    }
}

// The half `half` (0 previous, 1 next) of the bar is formatted: rdx the format, r8 the key's name, r9 the
// label.
template <std::size_t half>
void onFormat(HookRegisters& r) {
    if (r.rdx != g_keyTokenFormat || !mp_guard::allowsGameTouch()) {
        return;
    }
    const std::string_view key = stringAt(g_image, reinterpret_cast<const std::byte*>(r.r8), kKeyNameReach);
    const std::shared_ptr<const input::PromptLabelSet> labels = currentPromptLabels();
    if (!labels) {
        g_writtenUnnamed.store(true, std::memory_order_relaxed);
        return;
    }
    const input::KeyNameAnswer answer = input::keyNameAnswer(g_callers.loadTips[half], g_callers);
    const std::string* text = !key.empty() ? input::keyNameLabel(answer, key, *labels) : nullptr;
    if (text == nullptr || text->empty()) {
        return;
    }
    // The label set is never freed (prompt_hooks.cpp keeps every set published), and the format copies now.
    r.rdx = reinterpret_cast<std::uintptr_t>(kButtonFormat);
    r.r8 = reinterpret_cast<std::uintptr_t>(text->c_str());
    if (!g_textLogged[half].exchange(true, std::memory_order_relaxed)) {
        EVR_LOG("prompts: the loading screen's tips bar names %.*s: %s", static_cast<int>(key.size()),
                key.data(), text->c_str());
    }
}

// The format call of the bar's half whose label is `label`: the first `mov r8, rax; call` within kFormatReach
// of the label's load in `update`, with a load of the key-token format between. nullptr (logged) otherwise.
const std::byte* findFormatCall(const GameImage& image,
                                const std::byte* update,
                                std::string_view label,
                                const std::byte* keyTokenFormat,
                                const char* tag) {
    const std::byte* text = findUniqueString(image, label);
    const std::byte* load = nullptr;
    int loads = 0;
    for (const std::byte* at :
         text != nullptr ? findLeaReferences(image, text) : std::vector<const std::byte*>{}) {
        if (functionStart(image, at) == update) {
            load = at;
            ++loads;
        }
    }
    load = loads == 1 ? load : nullptr;
    const std::byte* call = nullptr;
    for (std::size_t i = 0; load != nullptr && call == nullptr && i < kFormatReach; ++i) {
        if (matchesAt(image, load + i, kFormatCall)) {
            call = load + i + 3;
        }
    }
    bool formatBetween = false;
    for (const std::byte* at :
         call != nullptr ? findLeaReferences(image, keyTokenFormat) : std::vector<const std::byte*>{}) {
        formatBetween = formatBetween || (at > load && at < call);
    }
    if (!formatBetween) {
        EVR_LOG(
            "%s: loading screen tips: no single format call after \"%.*s\"; that half keeps the game's key",
            tag, static_cast<int>(label.size()), label.data());
        return nullptr;
    }
    return call;
}

// The single match of `signature` in `update`'s own code within kUpdateSpan, or nullptr.
const std::byte* uniqueIn(const GameImage& image, const std::byte* update, const char* signature) {
    const std::byte* found = nullptr;
    int count = 0;
    for (std::size_t i = 0; i < kUpdateSpan && image.inText(update + i, 6); ++i) {
        if (matchesAt(image, update + i, signature) && functionStart(image, update + i) == update) {
            found = update + i;
            ++count;
        }
    }
    return count == 1 ? found : nullptr;
}

void installPoll(const GameImage& image, const std::byte* update, const char* tag) {
    const std::byte* poll = uniqueIn(image, update, kShoulderPoll);
    if (poll == nullptr || uniqueIn(image, update, kNextShoulderPoll) == nullptr) {
        EVR_LOG("%s: loading screen tips: the bar's key polls are not in its update; the grips send nothing "
                "there",
                tag);
        return;
    }
    std::string error;
    if (!installMidHook(const_cast<std::byte*>(poll), &onShoulderPoll, error)) {
        EVR_LOG("%s: loading screen tips: poll hook at RVA 0x%X failed: %s", tag, image.rva(poll),
                error.c_str());
        return;
    }
    g_pollInstalled.store(true, std::memory_order_release);
    EVR_LOG("%s: loading screen tips: key poll hooked at RVA 0x%X", tag, image.rva(poll));
}

// The hook that writes a bar again once the controllers' names are known, at the platform compare; without
// it such a bar keeps the game's keys.
void installRewrite(const GameImage& image, const std::byte* update, const char* tag) {
    const std::byte* compare = uniqueIn(image, update, kPlatformCompare);
    const std::int32_t field = compare != nullptr ? readI32(compare + 3) : 0;
    if (field <= 0 || field >= kMaxPlatformField) {
        EVR_LOG(
            "%s: loading screen tips: the bar's platform compare is not in its update; a bar written before "
            "the controllers' names are known keeps the game's keys",
            tag);
        return;
    }
    g_platformField = field;
    std::string error;
    if (!installMidHook(const_cast<std::byte*>(compare), &onPlatformCompare, error)) {
        g_platformField = 0;
        EVR_LOG("%s: loading screen tips: platform compare hook at RVA 0x%X failed: %s", tag,
                image.rva(compare), error.c_str());
        return;
    }
    EVR_LOG("%s: loading screen tips: platform compare hooked at RVA 0x%X (field +0x%X)", tag,
            image.rva(compare), static_cast<unsigned>(field));
}

void sendKeys(const std::vector<menu::LoadTipsKey>& keys) {
    for (const menu::LoadTipsKey& k : keys) {
        const bool sent = injectKey(k.key, k.down, gameWindow());
        if (g_keyLines < kKeyLines) {
            ++g_keyLines;
            EVR_LOG("prompts: loading screen tips: %c %s%s%s", static_cast<char>(k.key),
                    k.down ? "down" : "up", sent ? "" : " (not delivered)",
                    g_keyLines == kKeyLines ? " (the last such line)" : "");
        }
    }
}

} // namespace

void installLoadTipsHooks(const GameImage& image, const char* tag) {
    g_image = image;
    const std::byte* previous = findUniqueString(image, kPreviousLabel);
    const std::vector<const std::byte*> loads =
        previous != nullptr ? findLeaReferences(image, previous) : std::vector<const std::byte*>{};
    const std::byte* update = loads.size() == 1 ? functionStart(image, loads.front()) : nullptr;
    const std::byte* format = findUniqueString(image, kKeyTokenFormat);
    if (update == nullptr || format == nullptr) {
        EVR_LOG(
            "%s: loading screen tips: the bar's update or its key-token format is not found; the tips bar "
            "keeps the game's keys",
            tag);
        return;
    }
    EVR_LOG("%s: loading screen tips: the bar's update at RVA 0x%X", tag, image.rva(update));
    installPoll(image, update, tag);
    installRewrite(image, update, tag);
    g_keyTokenFormat = reinterpret_cast<std::uintptr_t>(format);
    const std::array<const std::byte*, 2> calls{findFormatCall(image, update, kPreviousLabel, format, tag),
                                                findFormatCall(image, update, kNextLabel, format, tag)};
    constexpr std::array<MidHookEditCallback, 2> kCallbacks{&onFormat<0>, &onFormat<1>};
    for (std::size_t half = 0; half < calls.size(); ++half) {
        std::string error;
        if (calls[half] == nullptr) {
            continue;
        }
        // Set before the hook goes in: the bar may be formatted on another thread at once.
        g_callers.loadTips[half] = reinterpret_cast<std::uintptr_t>(calls[half]) + 5;
        if (!installMidHookEdit(const_cast<std::byte*>(calls[half]), kCallbacks[half], error)) {
            g_callers.loadTips[half] = 0;
            EVR_LOG("%s: loading screen tips: format hook at RVA 0x%X failed: %s", tag,
                    image.rva(calls[half]), error.c_str());
        }
    }
    EVR_LOG("%s: loading screen tips: format calls at RVA 0x%X, 0x%X (0: the game's key there)", tag,
            g_callers.loadTips[0] != 0 ? image.rva(calls[0]) : 0u,
            g_callers.loadTips[1] != 0 ? image.rva(calls[1]) : 0u);
}

namespace {

// The game's first head-tracked view after a loading screen: the gap from the bar's last poll, once a load
// (from then on the bar counts as gone: features/menu/load_tips_keys.hpp).
void logGapToPlay(std::uint64_t polled, std::uint64_t tick) {
    if (polled == 0 || tick <= polled || polled == g_gapLoggedFor ||
        static_cast<double>(tick - polled) * 1e-6 > kGapLogSeconds) {
        return;
    }
    g_gapLoggedFor = polled;
    EVR_LOG("prompts: loading screen tips: the game's first view %.3f s after the bar's last poll; the grips "
            "send "
            "nothing from then on",
            static_cast<double>(tick - polled) * 1e-6);
}

} // namespace

void updateLoadTipsKeys(const std::optional<input::InputFrame>& frame,
                        double seconds,
                        bool menuUp,
                        bool shownGameView) {
    if (!g_pollInstalled.load(std::memory_order_acquire)) {
        return;
    }
    const std::uint64_t polled = g_polledAt.load(std::memory_order_relaxed);
    const std::uint64_t tick = stall_watch::lastGameTickMicros();
    const std::uint64_t now = window_timing::nowMicros();
    menu::LoadTipsInput in;
    in.seconds = seconds;
    in.sincePoll = polled != 0 && now >= polled ? static_cast<double>(now - polled) * 1e-6 : -1.0;
    in.menuUp = menuUp;
    // Play: the game rendered a head-tracked view since the bar's last poll (no loading screen does), or the
    // headset shows one.
    in.gameView = shownGameView || (polled != 0 && tick > polled);
    in.allowed = mp_guard::allowsGameTouch();
    logGapToPlay(polled, tick);
    if (frame) {
        in.leftGrip = frame->left.grip;
        in.rightGrip = frame->right.grip;
    }
    // Run every frame, the bar up or not: a grip held when the bar comes up is then not a press.
    sendKeys(g_keys.update(in));
}

void releaseLoadTipsKeys() {
    if (g_pollInstalled.load(std::memory_order_acquire)) {
        sendKeys(g_keys.release());
    }
}

} // namespace evr::vkcore::controllers
