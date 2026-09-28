#pragma once

// A problem found while reading or compiling binding text, worded for the log and the launcher.

#include <cstdint>
#include <string>
#include <utility>

namespace evr::input {

enum class BindingIssueKind : std::uint8_t {
    Syntax,       // The line is not `key = "value"`.
    DuplicateKey, // The same key appears twice in one text.
    UnknownKey,   // Not a hand, input or gesture we know.
    UnknownValue, // Not an action (or hand, or stick role) we know.
    Conflict,     // Valid on its own but clashes with another binding.
};

// Which rule a Conflict breaks (T-106: one unit test per kind).
enum class BindingConflict : std::uint8_t {
    None,
    PressWithTapOrHold,  // `press` and `tap`/`hold` on one input: the press fires on every tap and hold.
    SharedStickRole,     // Both sticks given the same role.
    GestureOffTurnStick, // A stick gesture on a stick that is not the turn stick.
    SharedInput,         // Two OpenXR actions of one set on one physical input (controller_bindings.hpp).
};

struct BindingIssue {
    BindingIssueKind kind = BindingIssueKind::Syntax;
    std::string key; // Empty when the line has no readable key.
    int line = 0;    // 1-based line in the text, or 0 when not from text.
    std::string message;

    // For a Conflict: the rule broken, and both sides named, so the launcher and the log can say which
    // two actions on which two inputs clash. `value` is what `key` binds (an action, or a stick role for
    // role keys); `otherKey` and `otherValue` are the binding it clashes with, the one that is kept.
    BindingConflict conflict = BindingConflict::None;
    std::string value;
    std::string otherKey;
    std::string otherValue;
};

// An issue that is not a conflict (the conflict fields stay empty).
inline BindingIssue issueOf(BindingIssueKind kind, std::string key, int line, std::string message) {
    BindingIssue issue;
    issue.kind = kind;
    issue.key = std::move(key);
    issue.line = line;
    issue.message = std::move(message);
    return issue;
}

} // namespace evr::input
