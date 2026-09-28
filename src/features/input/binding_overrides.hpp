#pragma once

// A player's bindings are a base map plus only the entries they changed, the same preset-plus-
// overrides idea as the settings profiles (ARCHITECTURE section 12). Improvements to a built-in map
// then reach every player except where they deliberately rebound something.
//
// An override replaces the base value for its key. The value "none" unbinds the key.

#include "features/input/binding_compiler.hpp"
#include "features/input/binding_text.hpp"

#include <string_view>

namespace evr::input {

BindingMap applyBindingOverrides(const BindingMap& base, const BindingMap& overrides);

// The overrides that turn `base` into `edited`: changed and added keys with their new values, and
// "none" for keys `edited` no longer has. The inverse of applyBindingOverrides.
BindingMap bindingOverrides(const BindingMap& base, const BindingMap& edited);

// Parses override text, applies it to `base` and compiles the result. Override keys that are not
// binding keys are reported and ignored, including ones that only unbind ("none"). Issues from all
// steps are returned together, with text line numbers where they came from the override text.
BindingBuildResult resolveBindings(const BindingMap& base, std::string_view overrideText);

} // namespace evr::input
