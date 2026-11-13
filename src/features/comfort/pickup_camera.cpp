#include "features/comfort/pickup_camera.hpp"

#include <cctype>
#include <cmath>
#include <cstddef>

namespace evr::comfort {

namespace {

bool isSpace(char c) {
    return std::isspace(static_cast<unsigned char>(c)) != 0;
}

// `text` without its surrounding spaces equals `name` (lower case), any case.
bool namedNoCase(std::string_view text, std::string_view name) {
    while (!text.empty() && isSpace(text.front())) {
        text.remove_prefix(1);
    }
    while (!text.empty() && isSpace(text.back())) {
        text.remove_suffix(1);
    }
    if (text.size() != name.size()) {
        return false;
    }
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(text[i])) != name[i]) {
            return false;
        }
    }
    return true;
}

} // namespace

std::optional<PickupCamera> parsePickupCamera(std::string_view text) {
    for (const PickupCamera camera : {PickupCamera::Still, PickupCamera::Play}) {
        if (namedNoCase(text, pickupCameraName(camera))) {
            return camera;
        }
    }
    return std::nullopt;
}

const char* pickupCameraName(PickupCamera camera) {
    return camera == PickupCamera::Play ? "play" : "still";
}

void PickupHeading::step(const Frame& f) {
    const double dt = f.seconds - lastSeconds_;
    lastSeconds_ = f.seconds;
    mapLoadsNow_ = f.mapLoads;
    menuNow_ = f.menu;
    if (f.started) {
        running_ = true;
        held_ = lastMapLoads_ == f.mapLoads ? last_ : std::nullopt; // not a heading from before a map load
        restoreUntil_ = -1.0;
        startEye_ = f.eye;
        startMapLoads_ = f.mapLoads;
        died_ = false;
    }
    if (running_) {
        died_ = died_ || f.dead;
    }
    if (f.ended && running_) {
        running_ = false;
        // With no world scale (0 or less) or an eye that is not finite the distance is not known: not
        // checked.
        const bool scaled = f.unitsPerMetre > 0.0f && std::isfinite(f.unitsPerMetre);
        const float moved = scaled ? length(f.eye - startEye_) / f.unitsPerMetre : 0.0f;
        moved_ = std::isfinite(moved) ? moved : 0.0f;
        dropped_ = Dropped::None;
        if (held_) { // only a held heading is dropped (and logged as such)
            if (f.mapLoads != startMapLoads_) {
                dropped_ = Dropped::MapLoad;
            } else if (died_) {
                dropped_ = Dropped::Died;
            } else if (moved_ > kMovedMetres) {
                dropped_ = Dropped::Moved;
            }
        }
        if (dropped_ != Dropped::None) {
            held_.reset();
        }
        restoreUntil_ = held_ ? f.seconds + kRestoreSeconds : -1.0;
    }
    if (!running_ && held_) {
        if (f.mapLoads != startMapLoads_ || f.dead) {
            held_.reset(); // a map load or a death in the window: the heading is another place's now
        } else if (f.menu && !f.ended && std::isfinite(dt) && dt > 0.0) {
            // The game paused: not the window's time. Not on the view the pickup ended, whose dt (which can
            // span views that were skipped) came before the window.
            restoreUntil_ += dt;
        }
    }
    if (!running_ && held_ && !(f.seconds < restoreUntil_)) {
        held_.reset();
    }
}

void PickupHeading::noteBody(float yaw, bool own) {
    if (running_ || held_) {
        return; // the pickup's views and the restore window's: the game's turn, not the player's
    }
    if (menuNow_ && !own) {
        return; // a menu holds the heading the player had when it came up (or faces a camera): kept as it was
    }
    last_ = own && std::isfinite(yaw) ? std::optional<float>(yaw) : std::nullopt;
    lastMapLoads_ = mapLoadsNow_;
}

std::optional<float> PickupHeading::restore(double seconds) const {
    return !running_ && seconds < restoreUntil_ ? held_ : std::nullopt;
}

void EndSetWindow::step(bool ended, bool forcedBefore, double seconds) {
    if (ended) {
        since_ = seconds;
        forced_ = false;
        return;
    }
    if (since_ < 0.0) {
        return;
    }
    const bool late = !(seconds - since_ <= kStartSeconds);
    if (forcedBefore && (forced_ || !late)) {
        forced_ = true; // the set's forced view goes on
    } else if (forced_ || late || forcedBefore) {
        since_ = -1.0; // it ended, or none started in time
        forced_ = false;
    }
}

bool EndSetWindow::covers(bool forcedNow, double seconds) const {
    return forcedNow && since_ >= 0.0 && (forced_ || seconds - since_ <= kStartSeconds);
}

std::optional<float> keptHeading(const KeptHeading& k) {
    if (k.pickupHeld && k.headAimed) {
        return k.pickupHeld;
    }
    if (k.gloryHeld) {
        return k.gloryHeld;
    }
    if (k.pickupRestore && k.headAimed && k.endSetView) {
        return k.pickupRestore;
    }
    return std::nullopt;
}

} // namespace evr::comfort
