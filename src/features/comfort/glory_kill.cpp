#include "features/comfort/glory_kill.hpp"

#include <cctype>
#include <cmath>
#include <cstddef>

namespace evr::comfort {

namespace {

std::string_view trim(std::string_view text) {
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front()))) {
        text.remove_prefix(1);
    }
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back()))) {
        text.remove_suffix(1);
    }
    return text;
}

bool equalsNoCase(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) {
        return false;
    }
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(a[i])) !=
            std::tolower(static_cast<unsigned char>(b[i]))) {
            return false;
        }
    }
    return true;
}

} // namespace

bool isKillSync(std::string_view entityDefName) {
    return !entityDefName.starts_with("interact/");
}

std::optional<GloryView> parseGloryView(std::string_view text) {
    const std::string_view t = trim(text);
    for (const GloryView view : {GloryView::Follow, GloryView::Steady, GloryView::Fade, GloryView::Screen}) {
        if (equalsNoCase(t, gloryViewName(view))) {
            return view;
        }
    }
    return std::nullopt;
}

const char* gloryViewName(GloryView view) {
    switch (view) {
    case GloryView::Follow:
        return "follow";
    case GloryView::Steady:
        return "steady";
    case GloryView::Fade:
        return "fade";
    case GloryView::Screen:
        return "screen";
    }
    return "follow";
}

GloryEpisode::GloryEpisode(GloryTiming timing) : timing_(timing) {}

GloryEpisode::Step GloryEpisode::update(bool sync, bool forcedView, double seconds) {
    Step step;
    if (!std::isfinite(seconds)) {
        step.active = active_;
        return step;
    }
    if (sync) {
        syncSeen_ = true;
        syncEndedAt_ = -1.0;
        if (!active_) {
            active_ = true;
            step.started = true;
            ++episodes_;
        }
    } else if (active_) {
        if (syncSeen_) {
            syncSeen_ = false;
            syncEndedAt_ = seconds;
        }
        const bool settling = forcedView && seconds - syncEndedAt_ < timing_.settleSeconds;
        if (!settling) {
            active_ = false;
            syncEndedAt_ = -1.0;
            step.ended = true;
        }
    }
    step.active = active_;
    return step;
}

void GloryEpisode::reset() {
    active_ = false;
    syncSeen_ = false;
    syncEndedAt_ = -1.0;
}

} // namespace evr::comfort
