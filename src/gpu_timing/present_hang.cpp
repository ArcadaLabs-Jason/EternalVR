#include "gpu_timing/present_hang.hpp"

#include "gpu_timing/present_stall.hpp"

#include <cstddef>

namespace evr::gpu_timing {

namespace {

double ms(std::uint64_t micros) {
    return static_cast<double>(micros) / 1000.0;
}

constexpr std::uint64_t kTestHangMaxMs = 600000;

// A whole number of digits only; nothing for an empty or overlong one.
std::optional<std::uint64_t> digitsOf(std::wstring_view text) {
    if (text.empty() || text.size() > 9) {
        return std::nullopt;
    }
    std::uint64_t value = 0;
    for (const wchar_t c : text) {
        if (c < L'0' || c > L'9') {
            return std::nullopt;
        }
        value = value * 10 + static_cast<std::uint64_t>(c - L'0');
    }
    return value;
}

// "<digits>[.<digits>]".
std::optional<double> secondsOf(std::wstring_view text) {
    const std::size_t dot = text.find(L'.');
    const std::optional<std::uint64_t> whole = digitsOf(text.substr(0, dot));
    if (!whole) {
        return std::nullopt;
    }
    double value = static_cast<double>(*whole);
    if (dot != std::wstring_view::npos) {
        const std::wstring_view fraction = text.substr(dot + 1);
        const std::optional<std::uint64_t> digits = digitsOf(fraction);
        if (!digits) {
            return std::nullopt;
        }
        double scale = 1.0;
        for (std::size_t i = 0; i < fraction.size(); ++i) {
            scale *= 10.0;
        }
        value += static_cast<double>(*digits) / scale;
    }
    return value;
}

} // namespace

std::optional<TestHang> parseTestHang(std::wstring_view value) {
    TestHang hang;
    const std::size_t at = value.find(L'@');
    if (at != std::wstring_view::npos) {
        const std::optional<double> seconds = secondsOf(value.substr(at + 1));
        if (!seconds) {
            return std::nullopt;
        }
        hang.atSecondsInMap = *seconds;
        value = value.substr(0, at);
    }
    const bool milliseconds = value.size() > 2 && value.substr(value.size() - 2) == L"ms";
    const std::optional<std::uint64_t> amount =
        digitsOf(milliseconds ? value.substr(0, value.size() - 2) : value);
    if (!amount) {
        return std::nullopt;
    }
    const std::uint64_t total = milliseconds ? *amount : *amount * 1000;
    if (total == 0 || total > kTestHangMaxMs) {
        return std::nullopt;
    }
    hang.ms = static_cast<std::uint32_t>(total);
    return hang;
}

HangResult HangWatch::onCheck(const HangCheck& check) {
    HangResult result;
    if (check.lastPresentMicros == 0) {
        return result;
    }
    if (notedPresent_ != 0 && check.lastPresentMicros != notedPresent_) {
        // A present after a noted gap: the game came back.
        result.action = HangAction::Resumed;
        result.gapMs =
            check.lastPresentMicros > notedPresent_ ? ms(check.lastPresentMicros - notedPresent_) : 0.0;
        result.hang = hangs_;
        notedPresent_ = 0;
        notes_ = 0;
        return result;
    }
    if (check.nowMicros <= check.lastPresentMicros) {
        return result;
    }
    const double gapMs = ms(check.nowMicros - check.lastPresentMicros);
    if (check.ticksKnown) {
        const bool tickNearPresent = check.lastTickMicros != 0 &&
                                     (check.lastTickMicros >= check.lastPresentMicros ||
                                      ms(check.lastPresentMicros - check.lastTickMicros) <= kInPlayTickMs);
        if (!tickNearPresent) {
            return result; // a loading screen or a menu
        }
    }
    const auto noted = [&](HangAction action) {
        result.action = action;
        result.gapMs = gapMs;
        result.tickAgeMs = check.lastTickMicros != 0 && check.nowMicros >= check.lastTickMicros
                               ? ms(check.nowMicros - check.lastTickMicros)
                               : -1.0;
        result.hang = hangs_;
        return result;
    };
    if (notedPresent_ == 0) {
        if (gapMs < kHangFirstNoteMs || hangs_ >= kHangsNoted) {
            return result;
        }
        notedPresent_ = check.lastPresentMicros;
        notes_ = 1;
        ++hangs_;
        return noted(HangAction::FirstNote);
    }
    if (notes_ == 1 && gapMs >= kHangSecondNoteMs) {
        notes_ = 2;
        return noted(HangAction::SecondNote);
    }
    return result;
}

} // namespace evr::gpu_timing
