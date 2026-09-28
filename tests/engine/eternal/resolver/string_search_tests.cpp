#include "engine/eternal/resolver/string_search.hpp"

#include "pe_builder.hpp"

#include <doctest/doctest.h>

#include <ostream>
#include <vector>

using evr::resolver::PeImage;
using evr::resolver::StringEncoding;
using evr::resolver::StringMatch;
using evr::resolver::StringQuery;

namespace {

constexpr std::uint32_t kRdataRva = 0x2000;

// .rdata layout (offsets from the section start):
//   0: "idPlayerHud\0"   (12 bytes)
//  12: "idPlayer\0"      (9 bytes)
//  21: padding           (1 byte; wide strings are 2-byte aligned)
//  22: L"idPlayer\0"     (18 bytes)
evr::test::PeSpec stringSpec() {
    evr::test::PeSpec spec;
    evr::test::SectionSpec text{".text", 0x1000, 0, {0xC3}};
    evr::test::SectionSpec rdata{".rdata", kRdataRva, 0, {}};
    evr::test::appendAscii(rdata.data, "idPlayerHud");
    evr::test::appendAscii(rdata.data, "idPlayer");
    rdata.data.push_back(0);
    evr::test::appendUtf16Le(rdata.data, "idPlayer");
    spec.sections = {text, rdata};
    return spec;
}

} // namespace

TEST_CASE("terminated ASCII search skips longer strings with the same prefix") {
    const auto bytes = evr::test::buildPe(stringSpec());
    const auto image = PeImage::parse(bytes);
    REQUIRE(image.ok());

    const auto rvas = evr::resolver::findStringInSection(*image, StringQuery::anchor("idPlayer"));
    CHECK(rvas == std::vector<std::uint32_t>{kRdataRva + 12});
}

TEST_CASE("unterminated ASCII search finds every occurrence") {
    const auto bytes = evr::test::buildPe(stringSpec());
    const auto image = PeImage::parse(bytes);
    REQUIRE(image.ok());

    const StringQuery query{"idPlayer", StringEncoding::Ascii, StringMatch::Anywhere};
    const auto rvas = evr::resolver::findStringInSection(*image, query);
    CHECK(rvas == std::vector<std::uint32_t>{kRdataRva, kRdataRva + 12});
}

TEST_CASE("UTF-16LE search finds wide strings only") {
    const auto bytes = evr::test::buildPe(stringSpec());
    const auto image = PeImage::parse(bytes);
    REQUIRE(image.ok());

    const StringQuery query{"idPlayer", StringEncoding::Utf16Le, StringMatch::Terminated};
    const auto rvas = evr::resolver::findStringInSection(*image, query);
    CHECK(rvas == std::vector<std::uint32_t>{kRdataRva + 22});
}

TEST_CASE("missing strings, empty queries and missing sections find nothing") {
    const auto bytes = evr::test::buildPe(stringSpec());
    const auto image = PeImage::parse(bytes);
    REQUIRE(image.ok());

    CHECK(evr::resolver::findStringInSection(*image, StringQuery::anchor("idDemon")).empty());
    CHECK(evr::resolver::findStringInSection(*image, StringQuery::anchor("")).empty());
    CHECK(evr::resolver::findStringInSection(*image, StringQuery::anchor("idPlayer"), ".data").empty());
    // Code bytes are not searched unless asked for.
    CHECK(evr::resolver::findStringInSection(*image, StringQuery::anchor("idPlayer"), ".text").empty());
}

TEST_CASE("UTF-16LE matches must start on a code unit boundary") {
    // ASCII "xA\0" then "B\0", then padding: the bytes 'A' 0 'B' 0 0 0 read as L"AB\0" from offset
    // 1, but that straddles code units and is not a wide string.
    evr::test::PeSpec spec;
    evr::test::SectionSpec rdata{".rdata", kRdataRva, 0, {}};
    evr::test::appendAscii(rdata.data, "xA");
    evr::test::appendAscii(rdata.data, "B");
    rdata.data.push_back(0);
    rdata.data.push_back(0);
    spec.sections = {rdata};
    const auto bytes = evr::test::buildPe(spec);
    const auto image = PeImage::parse(bytes);
    REQUIRE(image.ok());

    const StringQuery query{"AB", StringEncoding::Utf16Le, StringMatch::Terminated};
    CHECK(evr::resolver::findStringInSection(*image, query).empty());

    // The same text at an even offset is found.
    evr::test::PeSpec aligned;
    evr::test::SectionSpec alignedData{".rdata", kRdataRva, 0, {}};
    alignedData.data = {'x', 0};
    evr::test::appendUtf16Le(alignedData.data, "AB");
    aligned.sections = {alignedData};
    const auto alignedBytes = evr::test::buildPe(aligned);
    const auto alignedImage = PeImage::parse(alignedBytes);
    REQUIRE(alignedImage.ok());
    CHECK(evr::resolver::findStringInSection(*alignedImage, query) ==
          std::vector<std::uint32_t>{kRdataRva + 2});
}
