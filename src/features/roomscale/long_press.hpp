#pragma once

// A long press: fires once when an input has been held without a break for `seconds`, and again only
// after it was released. The recenter binding uses it, so a brush of the button never recenters.

namespace evr::roomscale {

class LongPress {
public:
    // A duration that is not finite and positive falls back to 1 s.
    explicit LongPress(float seconds = 1.0f);

    // True on the call where the input, down since `since`, has been down for `seconds`.
    bool update(bool down, double nowSeconds);

    [[nodiscard]] float seconds() const { return seconds_; }

private:
    float seconds_;
    bool down_ = false;
    bool fired_ = false;
    double since_ = 0.0;
};

} // namespace evr::roomscale
