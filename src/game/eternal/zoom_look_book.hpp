#pragma once

// The layer's record of the weapon decls it changed for the VR zoom look (zoom_look.hpp): what it wrote, the
// game's values to write back, and when it stops for good. The memory access is injected (ZoomDeclIo: the
// layer's fault-safe reads and writes, vkcore/zoom_look.cpp; a fake in the tests).
//
// - patch: a decl that reads the layer's values already is left alone; one that needs the look is written
//   only while the IO still allows it (the multiplayer guard), and a decl loaded again at the same address
//   (the game's values back) is written again with its new game values recorded.
// - A write that fails part way is rolled back to the game's values; if that fails too the decl is kept as
//   partial, and the restore writes the game's values without checking for the layer's.
// - restore: stops every later patch, then writes the game's values back only into a decl that still has its
//   name and the layer's values (not loaded again, not freed and reused).

#include "game/eternal/zoom_look.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace evr::game {

class ZoomDeclIo {
public:
    virtual ~ZoomDeclIo() = default;
    // The decl's name, or empty when it cannot be read.
    virtual std::string name(const void* decl) = 0;
    virtual bool read(const void* decl, ZoomDeclFields& fields) = 0;
    // False when any part of the write failed (the decl may be partly written).
    virtual bool write(const void* decl, const ZoomDeclFields& fields) = 0;
    // Asked right before each patch write: false once the multiplayer guard stops game touches.
    virtual bool mayWrite() = 0;
};

enum class ZoomPatch : std::uint8_t {
    Stopped,   // restore ran: no more writes
    NotNeeded, // the game's values need no change (zoomLookNeeded), or the look changes nothing
    Ours,      // the decl reads the layer's values already
    Refused,   // the IO no longer allows writes
    Written,   // written and recorded
    Failed,    // the write failed (rolled back, or kept as partial)
};

class ZoomPatchBook {
public:
    // `now`: the decl's fields as read (plausible, checked by the caller); `name` its checked name.
    ZoomPatch patch(const void* decl,
                    const std::string& name,
                    const ZoomDeclFields& now,
                    const ZoomLook& look,
                    ZoomDeclIo& io);

    // The game's own values of a decl the layer changed, or null.
    const ZoomDeclFields* gameValues(const void* decl) const;

    struct Restored {
        int back = 0;  // decls whose game values were written back
        int total = 0; // decls on record
    };
    Restored restore(ZoomDeclIo& io);

    bool stopped() const { return stopped_; }
    std::size_t size() const { return entries_.size(); }

private:
    struct Entry {
        const void* decl = nullptr;
        std::string name;
        ZoomDeclFields game;
        ZoomDeclFields ours;
        bool partial = false;
    };
    Entry* find(const void* decl);

    std::vector<Entry> entries_;
    bool stopped_ = false;
};

} // namespace evr::game
