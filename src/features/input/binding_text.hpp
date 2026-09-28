#pragma once

// Binding text: a [bindings] TOML table with one `"key" = "value"` per line, so bindings can sit in
// the settings file and also be edited by hand.
//
//   [bindings]
//   # Comments and blank lines are ignored.
//   "right.trigger.press" = "fire"
//   "right.stick.down_hold" = "weapon_wheel"   # Trailing comments too.
//
// Keys are written quoted. In TOML a bare `right.trigger.press` is a dotted key, which a TOML parser
// reads as nested tables (bindings.right.trigger.press) rather than one key, so the quoted form is
// the only one that means the same to this parser and to the settings file's TOML parser. The
// writer always uses it. The reader is lenient about hand edits: the table header may be left out,
// bare dotted keys are read as the same flat key, literal strings ('fire') work as well as basic
// ones, and a UTF-8 byte order mark and CRLF line endings are accepted. String escapes are not
// supported; binding names never need them.
//
// This layer only reads and writes text. It checks the line syntax and duplicate keys; what the keys
// and values mean is checked when compiling (binding_compiler.hpp). There is no file IO here.

#include "features/input/binding_issue.hpp"

#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace evr::input {

// Binding key to value, both as written.
using BindingMap = std::map<std::string, std::string, std::less<>>;

struct BindingTextResult {
    BindingMap entries;
    std::map<std::string, int, std::less<>> lineOf; // 1-based line each kept entry came from
    std::vector<BindingIssue> issues;
};

// Reads binding text. Lines with problems are reported and skipped; the rest are kept. For a
// duplicate key the first occurrence wins.
BindingTextResult parseBindingText(std::string_view text);

// Writes the [bindings] table header and the entries, with quoted keys, in a stable, readable
// order: weapon_hand first, then each hand's stick role, buttons and stick gestures, then any keys
// the grammar does not know, alphabetically.
std::string formatBindingText(const BindingMap& entries);

} // namespace evr::input
