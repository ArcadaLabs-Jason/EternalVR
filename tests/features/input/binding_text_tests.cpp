#include "features/input/binding_text.hpp"

#include <cstddef>
#include <doctest/doctest.h>
#include <ostream>

using evr::input::BindingIssueKind;
using evr::input::BindingMap;
using evr::input::BindingTextResult;
using evr::input::formatBindingText;
using evr::input::parseBindingText;

TEST_CASE("key/value lines are read, comments and blank lines skipped") {
    const BindingTextResult result = parseBindingText(R"(
# Weapon hand
right.trigger.press = "fire"   # trailing comment
  "right.stick.down_hold"="weapon_wheel"
weapon_hand = "right"
)");
    CHECK(result.issues.empty());
    CHECK(result.entries == BindingMap{{"right.trigger.press", "fire"},
                                       {"right.stick.down_hold", "weapon_wheel"},
                                       {"weapon_hand", "right"}});
    CHECK(result.lineOf.at("right.trigger.press") == 3);
    CHECK(result.lineOf.at("weapon_hand") == 5);
}

TEST_CASE("malformed lines are reported with their line number and skipped") {
    const BindingTextResult result = parseBindingText("right.trigger.press = \"fire\"\n"
                                                      "right.grip.press weapon_mod\n"
                                                      "right.primary.press = jump\n"
                                                      "right.secondary.press = \"dash\" extra\n"
                                                      "= \"melee\"\n"
                                                      "left.menu.press = \"pa\\\"use\"\n"
                                                      "left.trigger.press = \"equipment\n");
    CHECK(result.entries == BindingMap{{"right.trigger.press", "fire"}});
    REQUIRE(result.issues.size() == 6);
    for (int i = 0; i < 6; ++i) {
        CHECK(result.issues[static_cast<std::size_t>(i)].kind == BindingIssueKind::Syntax);
        CHECK(result.issues[static_cast<std::size_t>(i)].line == i + 2);
        CHECK_FALSE(result.issues[static_cast<std::size_t>(i)].message.empty());
    }
}

TEST_CASE("a duplicate key keeps the first value and is reported") {
    const BindingTextResult result = parseBindingText("right.trigger.press = \"fire\"\n"
                                                      "right.trigger.press = \"melee\"\n");
    CHECK(result.entries.at("right.trigger.press") == "fire");
    REQUIRE(result.issues.size() == 1);
    CHECK(result.issues[0].kind == BindingIssueKind::DuplicateKey);
    CHECK(result.issues[0].key == "right.trigger.press");
    CHECK(result.issues[0].line == 2);
}

TEST_CASE("formatting uses a readable, stable order") {
    const BindingMap entries{
        {"right.trigger.press", "fire"},
        {"zz.custom", "x"},
        {"left.stick.role", "move"},
        {"weapon_hand", "right"},
        {"left.trigger.press", "equipment"},
        {"right.stick.up", "chainsaw"},
        {"left.primary.tap", "switch_equipment"},
    };
    CHECK(formatBindingText(entries) == "[bindings]\n"
                                        "\"weapon_hand\" = \"right\"\n"
                                        "\"left.stick.role\" = \"move\"\n"
                                        "\"left.trigger.press\" = \"equipment\"\n"
                                        "\"left.primary.tap\" = \"switch_equipment\"\n"
                                        "\"right.trigger.press\" = \"fire\"\n"
                                        "\"right.stick.up\" = \"chainsaw\"\n"
                                        "\"zz.custom\" = \"x\"\n");
}

TEST_CASE("text needing escapes in a basic string is written as a literal string") {
    CHECK(formatBindingText({{"odd \"key\"", "a\\b"}}) == "[bindings]\n'odd \"key\"' = 'a\\b'\n");
}

TEST_CASE("format then parse gives the same entries") {
    const BindingMap entries{{"weapon_hand", "left"},
                             {"left.trigger.press", "fire"},
                             {"odd key", "x"},
                             {"it's", "fine"},
                             {"back\\slash", "q\"uote"}};
    const BindingTextResult reparsed = parseBindingText(formatBindingText(entries));
    CHECK(reparsed.issues.empty());
    CHECK(reparsed.entries == entries);
}

TEST_CASE("empty text is empty") {
    const BindingTextResult result = parseBindingText("");
    CHECK(result.entries.empty());
    CHECK(result.issues.empty());
}

TEST_CASE("a [bindings] table header is accepted and other tables are reported") {
    const BindingTextResult result = parseBindingText("[bindings]   # the table\n"
                                                      "\"right.trigger.press\" = \"fire\"\n"
                                                      "[comfort]\n");
    CHECK(result.entries == BindingMap{{"right.trigger.press", "fire"}});
    REQUIRE(result.issues.size() == 1);
    CHECK(result.issues[0].kind == BindingIssueKind::Syntax);
    CHECK(result.issues[0].line == 3);
}

TEST_CASE("quoted and bare dotted keys read as the same key") {
    const BindingTextResult quoted = parseBindingText("\"right.trigger.press\" = \"fire\"\n");
    const BindingTextResult bare = parseBindingText("right.trigger.press = \"fire\"\n");
    CHECK(quoted.entries == bare.entries);
}

TEST_CASE("a leading UTF-8 byte order mark is skipped") {
    const BindingTextResult result = parseBindingText("\xEF\xBB\xBFweapon_hand = \"left\"\n");
    CHECK(result.issues.empty());
    CHECK(result.entries == BindingMap{{"weapon_hand", "left"}});
}

TEST_CASE("TOML literal strings are accepted for keys and values") {
    const BindingTextResult result = parseBindingText("right.trigger.press = 'fire'\n"
                                                      "'left.grip.press' = 'flame_belch'   # comment\n"
                                                      "'back\\slash' = 'x'\n");
    CHECK(result.issues.empty());
    CHECK(result.entries == BindingMap{{"right.trigger.press", "fire"},
                                       {"left.grip.press", "flame_belch"},
                                       {"back\\slash", "x"}});
    // A literal string ends at the next single quote; unterminated ones are reported.
    CHECK(parseBindingText("right.trigger.press = 'fire\n").issues.size() == 1);
}

TEST_CASE("CRLF line endings are accepted") {
    const BindingTextResult result = parseBindingText("[bindings]\r\n"
                                                      "\"right.trigger.press\" = \"fire\"\r\n"
                                                      "\r\n"
                                                      "\"weapon_hand\" = \"right\" # hand\r\n");
    CHECK(result.issues.empty());
    CHECK(result.entries == BindingMap{{"right.trigger.press", "fire"}, {"weapon_hand", "right"}});
    CHECK(result.lineOf.at("weapon_hand") == 4);
}
