#pragma once

// The scan code the game reads for an injected key (vkcore/key_inject.cpp). The game numbers keys by the
// physical key: its raw keyboard handler takes the record's scan code, plus 0x80 for an E0 key, and never
// the virtual key or the keyboard layout (docs/rig-findings/menus.md section 7; its key names and the
// automap's W/A/S/D are those numbers). So a key is sent as the scan code of its place on a US keyboard:
// on a French AZERTY layout the layout's own scan code for 'W' is the key labelled W there, which the game
// reads as Z. Portable: no Windows types.

#include <cstdint>
#include <optional>

namespace evr::key_injection {

struct ScanCode {
    std::uint8_t make = 0; // set-1 make code
    bool e0 = false;       // an extended key (the arrows, Home, End and the like)
    friend constexpr bool operator==(const ScanCode&, const ScanCode&) = default;
};

// The US-keyboard scan code of a Windows virtual key: letters, digits, F1 to F12, the arrows and editing
// keys, Escape, Enter, Space, Tab, Backspace and the modifiers. Nothing for a key whose place differs
// between layouts by design (the OEM punctuation keys) or that the layer never sends.
std::optional<ScanCode> usScanCode(std::uint8_t virtualKey);

} // namespace evr::key_injection
