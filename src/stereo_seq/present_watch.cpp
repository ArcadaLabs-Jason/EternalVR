#include "stereo_seq/present_watch.hpp"

#include <cmath>
#include <cwchar>
#include <cwctype>
#include <string>

namespace evr::stereo_seq {

PresentWatch::Event PresentWatch::check(std::uint64_t presents, double seconds) {
    if (!started_ || presents != presents_) {
        const bool resumed = started_ && stopped_;
        quiet_ = resumed ? seconds - moved_ : 0.0;
        started_ = true;
        presents_ = presents;
        moved_ = seconds;
        stopped_ = false;
        return resumed ? Event::Resumed : Event::None;
    }
    if (stopped_ || seconds - moved_ < stopAfter_) {
        return Event::None;
    }
    stopped_ = true;
    quiet_ = seconds - moved_;
    return Event::Stopped;
}

std::optional<double> parseTestSeconds(std::wstring_view text) {
    while (!text.empty() && std::iswspace(text.front())) {
        text.remove_prefix(1);
    }
    while (!text.empty() && std::iswspace(text.back())) {
        text.remove_suffix(1);
    }
    if (text.empty()) {
        return std::nullopt;
    }
    const std::wstring copy(text);
    wchar_t* end = nullptr;
    const double seconds = std::wcstod(copy.c_str(), &end);
    if (end != copy.c_str() + copy.size() || !std::isfinite(seconds) || seconds <= 0.0 || seconds > 86400.0) {
        return std::nullopt;
    }
    return seconds;
}

bool OneShotAfter::due(double seconds) {
    if (done_) {
        return false;
    }
    if (!started_) {
        started_ = true;
        first_ = seconds;
    }
    if (seconds - first_ < after_) {
        return false;
    }
    done_ = true;
    return true;
}

} // namespace evr::stereo_seq
