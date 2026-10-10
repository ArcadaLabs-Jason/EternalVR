#include "vkcore/frame_repeat.hpp"

#include <doctest/doctest.h>

#include <cstddef>
#include <set>
#include <string>

using evr::vkcore::FrameRepeat;
using evr::vkcore::repeatName;

TEST_CASE("each repeat reason has its own word, without commas, for the frames table") {
    std::set<std::string> words;
    for (int i = 0; i <= static_cast<int>(FrameRepeat::Waiting); ++i) {
        const std::string word = repeatName(static_cast<FrameRepeat>(i));
        CHECK(word != "unknown");
        CHECK(word.find(',') == std::string::npos);
        CHECK(word.find(' ') == std::string::npos);
        words.insert(word);
    }
    CHECK(words.size() == static_cast<std::size_t>(FrameRepeat::Waiting) + 1);
    CHECK(std::string(repeatName(FrameRepeat::New)) == "new");
    CHECK(std::string(repeatName(FrameRepeat::Dropped)) == "dropped");
    CHECK(std::string(repeatName(static_cast<FrameRepeat>(200))) == "unknown");
}
