#include "vkcore/clone_census.hpp"

#include <doctest/doctest.h>

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace cc = evr::vkcore::clone_census;

namespace {

cc::Entry image(const std::string& name, std::uint64_t bytes) {
    cc::Entry e;
    e.name = name;
    e.bytes = bytes;
    return e;
}

cc::Entry target(const std::string& name, std::vector<std::uint32_t> parts) {
    cc::Entry e;
    e.name = name;
    e.target = true;
    e.parts = std::move(parts);
    return e;
}

} // namespace

TEST_CASE("the engine's texture formats by number, with their bytes per texel") {
    CHECK(std::string(cc::formatOf(0x14).name) == "R11FG11FB10F");
    CHECK(cc::formatOf(0x14).bytes == 4);
    CHECK(std::string(cc::formatOf(2).name) == "RGBA16F");
    CHECK(cc::formatOf(2).bytes == 8);
    CHECK(std::string(cc::formatOf(0x35).name) == "DEPTH32F");
    CHECK(cc::formatOf(0x13).bytes == 1);
    CHECK(cc::formatOf(0x1C).bytes == 8); // RG32F
    // Block-compressed: not estimated.
    CHECK(std::string(cc::formatOf(0x17).name) == "BC7");
    CHECK(cc::formatOf(0x17).bytes == 0);
    CHECK(std::string(cc::formatOf(0x38).name) == "?");
    CHECK(cc::formatOf(0xFFFFFFFFu).bytes == 0);
}

TEST_CASE("an image's bytes: every mip, layer and depth slice") {
    // 2056x2216 R11G11B10F, one mip: the eye-sized colour images.
    CHECK(cc::imageBytes(2056, 2216, 1, 1, 1, 4) == 2056ull * 2216 * 4);
    CHECK(cc::megabytes(cc::imageBytes(2056, 2216, 1, 1, 1, 4)) == "17.4 MB");
    // 256x256 with 6 mips: 256^2 + 128^2 + ... + 8^2.
    CHECK(cc::imageBytes(256, 256, 1, 1, 6, 4) == 4ull * (65536 + 16384 + 4096 + 1024 + 256 + 64));
    // A mip below 1 texel counts as 1 in that dimension.
    CHECK(cc::imageBytes(4, 1, 1, 1, 4, 1) == 4 + 2 + 1 + 1);
    CHECK(cc::imageBytes(128, 138, 64, 1, 1, 8) == 128ull * 138 * 64 * 8);
    CHECK(cc::imageBytes(16, 16, 1, 6, 1, 4) == 16ull * 16 * 6 * 4);
    // Nonsense counts are taken as 1.
    CHECK(cc::imageBytes(0, -3, 0, 0, 0, 4) == 4);
    CHECK(cc::imageBytes(10, 10, 1, 1, 1, 0) == 0);
}

TEST_CASE("an image is used by view 1 when stored, bound or marked, itself or through a target") {
    std::vector<cc::Entry> entries = {image("_frontcolor", 100), image("_gui", 50),
                                      image("_upscaledopaquedepth", 70), target("_gui", {1, 2}),
                                      image("_doflayerfar00", 30)};
    entries[0].uses[cc::kImageBinds] = 3;
    // The GUI target is bound, so both of its images count as used.
    entries[3].uses[cc::kTargetBinds] = 1;
    std::vector<bool> used = cc::usedByView1(entries);
    CHECK(used == std::vector<bool>{true, true, true, true, false});
    const cc::Summary unused = cc::unusedImages(entries, 10);
    CHECK(unused.count == 1);
    CHECK(unused.bytes == 30);
    CHECK(unused.names == "_doflayerfar00");
    CHECK(cc::unusedTargets(entries) == 0);
    // Seen on another context only: not a use by view 1.
    entries[4].uses[cc::kOtherContexts] = 5;
    CHECK(cc::unusedImages(entries, 10).count == 1);
    // Passed as the clone itself: a use.
    entries[4].uses[cc::kAsClone] = 1;
    CHECK(cc::unusedImages(entries, 10).count == 0);
}

TEST_CASE("a target never stored or bound leaves its images unused unless they are used themselves") {
    std::vector<cc::Entry> entries = {image("a", 10), image("b", 20), target("a", {0, 1})};
    CHECK(cc::unusedTargets(entries) == 1);
    entries[1].uses[cc::kMarks] = 2;
    const cc::Summary unused = cc::unusedImages(entries, 10);
    CHECK(unused.count == 1);
    CHECK(unused.names == "a");
    // A part index past the entries is ignored.
    entries[2].parts.push_back(99);
    entries[2].uses[cc::kStored] = 1;
    CHECK(cc::unusedImages(entries, 10).count == 0);
}

TEST_CASE("a left-out object is touched by view 1 when it stored, bound or marked it") {
    std::vector<cc::Entry> entries = {target("accumulationbuffer00", {1}), image("accumulationbuffer00", 36),
                                      target("distortion0", {})};
    CHECK(cc::touched(entries, 5).count == 0);
    entries[1].uses[cc::kImageBinds] = 4;
    entries[2].uses[cc::kStored] = 1;
    // Another context's use is not view 1's.
    entries[0].uses[cc::kOtherContexts] = 9;
    const cc::Summary s = cc::touched(entries, 5);
    CHECK(s.count == 2);
    CHECK(s.names == "accumulationbuffer00, distortion0");
    CHECK(s.bytes == 36);
}

TEST_CASE("the summaries list at most maxNames names, then an ellipsis; a nameless clone shows as ?") {
    std::vector<cc::Entry> entries = {image("a", 1), image("", 2), image("c", 3), image("d", 4)};
    const cc::Summary s = cc::unusedImages(entries, 2);
    CHECK(s.count == 4);
    CHECK(s.bytes == 10);
    CHECK(s.names == "a, ?, ...");
    CHECK(cc::unusedImages(entries, 0).names == "...");
    CHECK(cc::otherContexts(entries, 5).count == 0);
    entries[2].uses[cc::kOtherContexts] = 1;
    entries.push_back(target("t", {}));
    entries.back().uses[cc::kOtherContexts] = 7;
    const cc::Summary other = cc::otherContexts(entries, 5);
    CHECK(other.count == 2);
    CHECK(other.names == "c, t");
}
