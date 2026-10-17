#include "game/eternal/zoom_look_book.hpp"

#include <doctest/doctest.h>

#include <cstdint>
#include <map>
#include <string>

using evr::game::kScopeGuiNone;
using evr::game::ZoomDeclFields;
using evr::game::ZoomDeclIo;
using evr::game::ZoomLook;
using evr::game::ZoomPatch;
using evr::game::ZoomPatchBook;

namespace {

constexpr std::uintptr_t kReticle = 0x1000;
constexpr std::uintptr_t kZoomedReticle = 0x2000;

// weapon/player/heavy_cannon_bolt_action as the rig read it.
ZoomDeclFields bolt() {
    ZoomDeclFields f;
    f.hideHandsDelay = 73997;
    f.hideHandsDelayMs = 74;
    f.scopeGuiNum = 2;
    f.reticle = kReticle;
    f.reticleWhenZoomed = kZoomedReticle;
    return f;
}

// weapon/player/heavy_cannon: a 150 ms delay, no scope, no hide at the start: left alone.
ZoomDeclFields cannon() {
    ZoomDeclFields f;
    f.hideHandsDelay = 149997;
    f.hideHandsDelayMs = 150;
    f.reticle = kReticle;
    return f;
}

// Game memory for the book: a decl's name and fields, writes that can fail (all of them, or only after the
// first `writesBeforeFailure`), and the guard's answer.
struct FakeIo final : ZoomDeclIo {
    std::map<const void*, std::string> names;
    std::map<const void*, ZoomDeclFields> fields;
    bool allow = true;
    int writesBeforeFailure = -1; // -1: never fails
    int partialsLeft = 1;         // failed writes that still land their first field
    int writes = 0;

    std::string name(const void* decl) override {
        const auto it = names.find(decl);
        return it == names.end() ? std::string() : it->second;
    }
    bool read(const void* decl, ZoomDeclFields& f) override {
        const auto it = fields.find(decl);
        if (it == fields.end()) {
            return false;
        }
        f = it->second;
        return true;
    }
    bool write(const void* decl, const ZoomDeclFields& f) override {
        if (writesBeforeFailure >= 0 && writes >= writesBeforeFailure) {
            // A partial write: the first field lands, the rest does not.
            if (partialsLeft > 0) {
                --partialsLeft;
                fields[decl].hideHandsDelay = f.hideHandsDelay;
            }
            return false;
        }
        ++writes;
        fields[decl] = f;
        return true;
    }
    bool mayWrite() override { return allow; }
};

int g_boltDecl = 1;
int g_cannonDecl = 2;
const void* const kBolt = &g_boltDecl;
const void* const kCannon = &g_cannonDecl;
const std::string kBoltName = "weapon/player/heavy_cannon_bolt_action";

FakeIo memory() {
    FakeIo io;
    io.names[kBolt] = kBoltName;
    io.fields[kBolt] = bolt();
    io.names[kCannon] = "weapon/player/heavy_cannon";
    io.fields[kCannon] = cannon();
    return io;
}

ZoomPatch patchNow(ZoomPatchBook& book, FakeIo& io, const void* decl) {
    return book.patch(decl, io.names[decl], io.fields[decl], ZoomLook{}, io);
}

} // namespace

TEST_CASE("zoom book: the Bolt is written once and recorded; the cannon's 150 ms delay is left alone") {
    FakeIo io = memory();
    ZoomPatchBook book;
    CHECK(patchNow(book, io, kBolt) == ZoomPatch::Written);
    CHECK(io.fields[kBolt].hideHandsDelay == 0);
    CHECK(io.fields[kBolt].scopeGuiNum == kScopeGuiNone);
    CHECK(io.fields[kBolt].reticleWhenZoomed == kReticle);
    REQUIRE(book.gameValues(kBolt));
    CHECK(*book.gameValues(kBolt) == bolt());
    CHECK(patchNow(book, io, kBolt) == ZoomPatch::Ours);
    CHECK(io.writes == 1);
    CHECK(patchNow(book, io, kCannon) == ZoomPatch::NotNeeded);
    CHECK(io.fields[kCannon] == cannon());
    CHECK(book.size() == 1);
}

TEST_CASE("zoom book: a decl loaded again at the same address is written again, its new values recorded") {
    FakeIo io = memory();
    ZoomPatchBook book;
    REQUIRE(patchNow(book, io, kBolt) == ZoomPatch::Written);
    ZoomDeclFields reloaded = bolt();
    reloaded.hideHandsDelayMs = 80;
    reloaded.hideHandsDelay = 79997;
    io.fields[kBolt] = reloaded;
    CHECK(patchNow(book, io, kBolt) == ZoomPatch::Written);
    CHECK(*book.gameValues(kBolt) == reloaded);
    CHECK(book.size() == 1);
    // The restore puts back the values of the latest load.
    CHECK(book.restore(io).back == 1);
    CHECK(io.fields[kBolt] == reloaded);
}

TEST_CASE("zoom book: the restore writes back only decls that still have their name and the layer's values") {
    FakeIo io = memory();
    ZoomPatchBook book;
    REQUIRE(patchNow(book, io, kBolt) == ZoomPatch::Written);
    SUBCASE("still ours") {
        const ZoomPatchBook::Restored r = book.restore(io);
        CHECK(r.back == 1);
        CHECK(r.total == 1);
        CHECK(io.fields[kBolt] == bolt());
    }
    SUBCASE("loaded again: the game's values are there and stay") {
        ZoomDeclFields fresh = bolt();
        fresh.scopeGuiNum = 3;
        io.fields[kBolt] = fresh;
        const ZoomPatchBook::Restored r = book.restore(io);
        CHECK(r.back == 0);
        CHECK(r.total == 1);
        CHECK(io.fields[kBolt] == fresh);
    }
    SUBCASE("freed and reused under another name: not written") {
        io.names[kBolt] = "weapon/player/shotgun";
        const ZoomDeclFields other = io.fields[kBolt];
        CHECK(book.restore(io).back == 0);
        CHECK(io.fields[kBolt] == other);
    }
}

TEST_CASE("zoom book: after the restore nothing is written again") {
    FakeIo io = memory();
    ZoomPatchBook book;
    REQUIRE(patchNow(book, io, kBolt) == ZoomPatch::Written);
    book.restore(io);
    CHECK(book.stopped());
    CHECK(book.size() == 0);
    CHECK(patchNow(book, io, kBolt) == ZoomPatch::Stopped);
    CHECK(io.fields[kBolt] == bolt());
    // A second restore (the presenter after the trip listener) finds nothing.
    CHECK(book.restore(io).total == 0);
}

TEST_CASE("zoom book: no write once the guard stops game touches") {
    FakeIo io = memory();
    io.allow = false;
    ZoomPatchBook book;
    CHECK(patchNow(book, io, kBolt) == ZoomPatch::Refused);
    CHECK(io.writes == 0);
    CHECK(io.fields[kBolt] == bolt());
    CHECK(book.size() == 0);
}

TEST_CASE("zoom book: a failed write is rolled back, or kept as partial and put back by the restore") {
    FakeIo io = memory();
    ZoomPatchBook book;
    SUBCASE("rolled back") {
        io.writesBeforeFailure = 0; // the patch write fails part way...
        // ...and the rollback is allowed again.
        struct RollbackIo final : ZoomDeclIo {
            FakeIo& inner;
            bool first = true;
            explicit RollbackIo(FakeIo& io) : inner(io) {}
            std::string name(const void* d) override { return inner.name(d); }
            bool read(const void* d, ZoomDeclFields& f) override { return inner.read(d, f); }
            bool write(const void* d, const ZoomDeclFields& f) override {
                if (first) {
                    first = false;
                    return inner.write(d, f);
                }
                inner.writesBeforeFailure = -1;
                return inner.write(d, f);
            }
            bool mayWrite() override { return true; }
        } rollback(io);
        CHECK(book.patch(kBolt, kBoltName, bolt(), ZoomLook{}, rollback) == ZoomPatch::Failed);
        CHECK(io.fields[kBolt] == bolt());
        CHECK(book.size() == 0);
    }
    SUBCASE("kept as partial") {
        io.writesBeforeFailure = 0; // the write and its rollback both fail part way
        CHECK(patchNow(book, io, kBolt) == ZoomPatch::Failed);
        CHECK(io.fields[kBolt] != bolt());
        REQUIRE(book.gameValues(kBolt));
        CHECK(*book.gameValues(kBolt) == bolt());
        io.writesBeforeFailure = -1;
        const ZoomPatchBook::Restored r = book.restore(io);
        CHECK(r.back == 1);
        CHECK(io.fields[kBolt] == bolt());
    }
}

TEST_CASE("zoom book: the game's look (off) writes nothing") {
    FakeIo io = memory();
    ZoomPatchBook book;
    const ZoomLook off{false, false};
    CHECK(book.patch(kBolt, kBoltName, bolt(), off, io) == ZoomPatch::NotNeeded);
    CHECK(io.writes == 0);
}
