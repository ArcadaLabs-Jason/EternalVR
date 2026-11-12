#include "game/eternal/zoom_look_book.hpp"

#include <cstddef>
#include <exception>
#include <utility>

namespace evr::game {

ZoomPatchBook::Entry* ZoomPatchBook::find(const void* decl) {
    for (Entry& e : entries_) {
        if (e.decl == decl) {
            return &e;
        }
    }
    return nullptr;
}

ZoomPatch ZoomPatchBook::patch(const void* decl,
                               const std::string& name,
                               const ZoomDeclFields& now,
                               const ZoomLook& look,
                               ZoomDeclIo& io) {
    if (stopped_) {
        return ZoomPatch::Stopped;
    }
    Entry* known = find(decl);
    if (known && now == known->ours && known->name == name) {
        known->partial = false;
        return ZoomPatch::Ours;
    }
    // A partial write left the decl unknown: its game values are the recorded ones. Otherwise what it reads
    // now is the game's (first sight, or loaded again at the same address).
    const ZoomDeclFields game = known && known->partial && known->name == name ? known->game : now;
    const ZoomDeclFields ours = zoomLookFields(game, look);
    if (ours == game) {
        if (known) {
            entries_.erase(entries_.begin() + (known - entries_.data()));
        }
        return ZoomPatch::NotNeeded;
    }
    if (!io.mayWrite()) {
        return ZoomPatch::Refused;
    }
    if (!io.write(decl, ours)) {
        const bool rolledBack = io.write(decl, game);
        if (rolledBack) {
            if (known) {
                entries_.erase(entries_.begin() + (known - entries_.data()));
            }
        } else if (known) {
            *known = {decl, name, game, ours, true};
        } else {
            entries_.push_back({decl, name, game, ours, true});
        }
        return ZoomPatch::Failed;
    }
    if (known) {
        *known = {decl, name, game, ours, false};
    } else {
        entries_.push_back({decl, name, game, ours, false});
    }
    return ZoomPatch::Written;
}

const ZoomDeclFields* ZoomPatchBook::gameValues(const void* decl) const {
    for (const Entry& e : entries_) {
        if (e.decl == decl) {
            return &e.game;
        }
    }
    return nullptr;
}

ZoomPatchBook::Restored ZoomPatchBook::restore(ZoomDeclIo& io) {
    stopped_ = true;
    Restored r;
    r.total = static_cast<int>(entries_.size());
    // Each decl on its own: one whose name cannot be read (no memory for it) cannot be checked yet, so it is
    // not written (it may have been freed) and stays on record for the next restore, and the others still go
    // back. A trip runs this from the game's hook.
    std::size_t kept = 0;
    for (std::size_t i = 0; i < entries_.size(); ++i) {
        const Entry& e = entries_[i];
        bool same = false;
        try {
            same = io.name(e.decl) == e.name;
        } catch (const std::exception&) {
            ++r.failed;
            if (kept != i) {
                entries_[kept] = std::move(entries_[i]); // a move: no allocation
            }
            ++kept;
            continue;
        }
        if (!same) {
            continue; // freed or reused: not the layer's to write
        }
        ZoomDeclFields now;
        if (!e.partial && (!io.read(e.decl, now) || now != e.ours)) {
            continue; // loaded again: the game's values are there
        }
        if (io.write(e.decl, e.game)) {
            ++r.back;
        }
    }
    entries_.erase(entries_.begin() + static_cast<std::ptrdiff_t>(kept), entries_.end());
    return r;
}

} // namespace evr::game
