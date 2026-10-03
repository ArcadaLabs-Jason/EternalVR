#include "platform/key_injection/us_scan_codes.hpp"

#include <array>

namespace evr::key_injection {

namespace {

// Set-1 make codes of A to Z and of 0 to 9 on a US keyboard.
constexpr std::array<std::uint8_t, 26> kLetters = {0x1E, 0x30, 0x2E, 0x20, 0x12, 0x21, 0x22, 0x23, 0x17,
                                                   0x24, 0x25, 0x26, 0x32, 0x31, 0x18, 0x19, 0x10, 0x13,
                                                   0x1F, 0x14, 0x16, 0x2F, 0x11, 0x2D, 0x15, 0x2C};
constexpr std::array<std::uint8_t, 10> kDigits = {0x0B, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A};

// Windows virtual-key codes (winuser.h), so this file needs no Windows header.
enum Vk : std::uint8_t {
    kBack = 0x08,
    kTab = 0x09,
    kReturn = 0x0D,
    kShift = 0x10,
    kControl = 0x11,
    kMenu = 0x12,
    kCapital = 0x14,
    kEscape = 0x1B,
    kSpace = 0x20,
    kPrior = 0x21,
    kNext = 0x22,
    kEnd = 0x23,
    kHome = 0x24,
    kLeft = 0x25,
    kUp = 0x26,
    kRight = 0x27,
    kDown = 0x28,
    kInsert = 0x2D,
    kDelete = 0x2E,
    kF1 = 0x70,
    kF10 = 0x79,
    kF11 = 0x7A,
    kF12 = 0x7B,
    kLShift = 0xA0,
    kRShift = 0xA1,
    kLControl = 0xA2,
    kRControl = 0xA3,
    kLMenu = 0xA4,
    kRMenu = 0xA5,
};

} // namespace

std::optional<ScanCode> usScanCode(std::uint8_t virtualKey) {
    if (virtualKey >= 'A' && virtualKey <= 'Z') {
        return ScanCode{kLetters[virtualKey - 'A'], false};
    }
    if (virtualKey >= '0' && virtualKey <= '9') {
        return ScanCode{kDigits[virtualKey - '0'], false};
    }
    if (virtualKey >= kF1 && virtualKey <= kF10) {
        return ScanCode{static_cast<std::uint8_t>(0x3B + (virtualKey - kF1)), false};
    }
    switch (virtualKey) {
    case kF11:
        return ScanCode{0x57, false};
    case kF12:
        return ScanCode{0x58, false};
    case kEscape:
        return ScanCode{0x01, false};
    case kBack:
        return ScanCode{0x0E, false};
    case kTab:
        return ScanCode{0x0F, false};
    case kReturn:
        return ScanCode{0x1C, false};
    case kSpace:
        return ScanCode{0x39, false};
    case kCapital:
        return ScanCode{0x3A, false};
    case kShift:
    case kLShift:
        return ScanCode{0x2A, false};
    case kRShift:
        return ScanCode{0x36, false};
    case kControl:
    case kLControl:
        return ScanCode{0x1D, false};
    case kRControl:
        return ScanCode{0x1D, true};
    case kMenu:
    case kLMenu:
        return ScanCode{0x38, false};
    case kRMenu:
        return ScanCode{0x38, true};
    case kUp:
        return ScanCode{0x48, true};
    case kDown:
        return ScanCode{0x50, true};
    case kLeft:
        return ScanCode{0x4B, true};
    case kRight:
        return ScanCode{0x4D, true};
    case kHome:
        return ScanCode{0x47, true};
    case kEnd:
        return ScanCode{0x4F, true};
    case kPrior:
        return ScanCode{0x49, true};
    case kNext:
        return ScanCode{0x51, true};
    case kInsert:
        return ScanCode{0x52, true};
    case kDelete:
        return ScanCode{0x53, true};
    default:
        return std::nullopt;
    }
}

} // namespace evr::key_injection
