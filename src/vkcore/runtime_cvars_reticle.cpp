#include "vkcore/runtime_cvars_reticle.hpp"

#include "ui_layer/crosshair.hpp"
#include "ui_layer/ui_settings.hpp"
#include "vkcore/cvar_book.hpp"
#include "vkcore/log.hpp"
#include "vkcore/runtime_cvars_impl.hpp"

#include <windows.h>

#include <atomic>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace evr::vkcore::runtime_cvars {

namespace {

constexpr int kLoggedTurns = 12; // writes after the first: the first 12 logged, later ones counted

// The presenter's request, when it was made (GetTickCount64) and the presents that read it since, up to
// kHoldStalePresents (ui_layer::holdRequested).
std::atomic<bool> g_want{false};
std::atomic<std::uint64_t> g_wantMs{0};
std::atomic<std::uint32_t> g_wantPresents{0};
// The run of presents that read the cvar at 0 while held (ui_layer::nextHoldStreak) and when the last
// present checked it (GetTickCount64): written under heldMutex(), read by the presenter without it.
std::atomic<std::uint32_t> g_streak{0};
std::atomic<std::uint64_t> g_checkedMs{0};
int g_loggedTurns = 0;
std::uint64_t g_turns = 0;

void record(bool holding, bool hidden) {
    const std::uint32_t streak =
        ui_layer::nextHoldStreak(g_streak.load(std::memory_order_relaxed), holding, hidden);
    g_checkedMs.store(GetTickCount64(), std::memory_order_relaxed);
    g_streak.store(streak, std::memory_order_release);
}

} // namespace

void addReticleHold(std::vector<Held>& held) {
    // The presenter's own settings (ETERNALVR_UI_LAYER and its default, ETERNALVR_UI_CROSSHAIR): without the
    // UI layer nothing would ever ask for the hold, so it is not listed either. Its warnings are the
    // presenter's.
    std::vector<std::string> warnings;
    const ui_layer::UiSettings ui = ui_layer::readUiSettings(
        [](std::wstring_view name) -> std::optional<std::wstring> {
            std::wstring text;
            if (!readEnv(std::wstring(name).c_str(), text)) {
                return std::nullopt;
            }
            return text;
        },
        warnings);
    if (!ui.enabled || ui.crosshair != ui_layer::CrosshairMode::Hold) {
        return;
    }
    Held h;
    h.name = ui_layer::kReticleStyleCvar;
    h.value = std::to_string(ui_layer::kReticleStyleHidden);
    h.reticle = true;
    held.push_back(std::move(h));
}

bool applyReticle(Held& h, cvar_book::SetStringFn set) {
    const bool asked = g_want.load(std::memory_order_relaxed);
    const std::uint32_t presents = g_wantPresents.load(std::memory_order_relaxed);
    const std::uint32_t next = ui_layer::nextRequestPresents(presents);
    if (next != presents) {
        // Both callers (this and wantReticleHidden, the GUI copy after it) run on the presenting thread
        // today, so no request lands in between. One made from another thread between the load and the
        // exchange keeps its count of 0, or gets 1 when the count it replaced was 0 too: one present more,
        // harmless, since the request must also be over kHoldStaleMs old to lapse.
        std::uint32_t seen = presents;
        g_wantPresents.compare_exchange_strong(seen, next, std::memory_order_relaxed);
    }
    const bool want =
        ui_layer::holdRequested(asked, g_wantMs.load(std::memory_order_relaxed), GetTickCount64(), presents);
    const int before = readValue(h.object);
    const char* why = nullptr;
    std::string value;
    const ui_layer::ReticleWrite turn = ui_layer::reticleWrite(want, before, h.reticleKeep);
    switch (turn) {
    case ui_layer::ReticleWrite::Hide:
        value = h.value;
        why = "hand aim: the game's crosshair hidden";
        break;
    case ui_layer::ReticleWrite::Restore:
        static_assert(ui_layer::kHoldStaleMs == 500 && ui_layer::kHoldStalePresents == 4, "named in the log");
        value = std::to_string(h.reticleKeep.before);
        why = asked ? "the hold's request lapsed (no GUI copy for 0.5 s and 4 presents): the game's "
                      "crosshair back"
                    : "the hold's request ended (no hand aim): the game's crosshair back";
        break;
    case ui_layer::ReticleWrite::None:
        break;
    }
    bool wrote = false;
    if (why) {
        wrote = cvar_book::write(h.name, h.object, set, value.c_str());
        if (!wrote && turn == ui_layer::ReticleWrite::Restore) {
            h.reticleKeep.kept = true; // not given back yet: the next present tries again
        }
        if (wrote) {
            ++g_turns;
            if (!h.written || g_loggedTurns < kLoggedTurns) {
                g_loggedTurns += h.written ? 1 : 0;
                h.written = true;
                EVR_LOG("%s: %s %d -> %s (reads %d); %s (%llu write(s))", kTag, h.name.c_str(), before,
                        value.c_str(), readValue(h.object), why, static_cast<unsigned long long>(g_turns));
            }
        }
    }
    // Only a present that already read 0 before its own write counts: the frames since the last one were
    // drawn without the crosshair.
    record(want, before == ui_layer::kReticleStyleHidden);
    return wrote;
}

void reticleUnchecked() {
    record(false, false);
}

void wantReticleHidden(bool want) {
    g_wantMs.store(GetTickCount64(), std::memory_order_relaxed);
    g_wantPresents.store(0, std::memory_order_relaxed);
    g_want.store(want, std::memory_order_relaxed);
}

bool reticleHiddenConfirmed() {
    const std::uint32_t streak = g_streak.load(std::memory_order_acquire);
    return ui_layer::holdConfirmed(streak, g_checkedMs.load(std::memory_order_relaxed), GetTickCount64());
}

} // namespace evr::vkcore::runtime_cvars
