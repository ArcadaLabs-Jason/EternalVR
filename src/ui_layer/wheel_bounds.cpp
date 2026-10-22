#include "ui_layer/wheel_bounds.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace evr::ui_layer {

namespace {

// The seeds on the centre row: from the band's x 0.93 (inside the wheel's ring at any size seen, its right
// edge staying near 0.935) leftwards to 0.62.
constexpr float kSeedFrom = 0.93f;
constexpr float kSeedTo = 0.62f;
// A wheel's size across, as a fraction of the band's width, and how far from round its box may be.
constexpr double kMinAcross = 0.10;
constexpr double kMaxAcross = 0.60;
constexpr double kMaxAspect = 4.0 / 3.0;
// A shape wider than this times its height is the wheel with something drawn left of it touching it (the
// crosshair's rings end about 0.012 of the band's width left of a larger HUD's wheel): a highlighted
// segment's bar makes the wheel itself at most 3% wider than tall on the rig's captures.
constexpr double kMergedAspect = 1.06;
// Shapes apart from the ring that lie wholly within this fraction of its height around it belong to the
// wheel (a tab, a label); none was seen on the rig's or a player's captures, where the nearest other HUD
// element is 5.9% of the wheel's height away.
constexpr double kGatherFraction = 0.025;

using Box = WheelScratch::Box;

Box flood(const AlphaGrid& g, std::uint32_t c, std::uint32_t r, std::uint32_t label, WheelScratch& s) {
    Box box{c, r, c, r};
    s.stack.clear();
    s.stack.push_back(r * g.columns + c);
    s.label[s.stack.back()] = label;
    while (!s.stack.empty()) {
        const std::uint32_t at = s.stack.back();
        s.stack.pop_back();
        const std::uint32_t ac = at % g.columns;
        const std::uint32_t ar = at / g.columns;
        box.c0 = std::min(box.c0, ac);
        box.c1 = std::max(box.c1, ac);
        box.r0 = std::min(box.r0, ar);
        box.r1 = std::max(box.r1, ar);
        for (int dr = -1; dr <= 1; ++dr) {
            for (int dc = -1; dc <= 1; ++dc) {
                const auto nc = static_cast<std::int64_t>(ac) + dc;
                const auto nr = static_cast<std::int64_t>(ar) + dr;
                if (nc < 0 || nr < 0 || nc >= g.columns || nr >= g.rows) {
                    continue;
                }
                const auto n = static_cast<std::uint32_t>(nr * g.columns + nc);
                if (s.label[n] == 0 && g.alpha[n] >= kWheelMinAlpha) {
                    s.label[n] = label;
                    s.stack.push_back(n);
                }
            }
        }
    }
    return box;
}

// The box of the samples labelled `label` in `b` from column `c0` on.
Box clipLeft(const AlphaGrid& g, const WheelScratch& s, std::uint32_t label, const Box& b, std::uint32_t c0) {
    Box out{b.c1, b.r1, c0, b.r0};
    for (std::uint32_t r = b.r0; r <= b.r1; ++r) {
        for (std::uint32_t c = c0; c <= b.c1; ++c) {
            if (s.label[std::size_t{r} * g.columns + c] == label) {
                out.c0 = std::min(out.c0, c);
                out.c1 = std::max(out.c1, c);
                out.r0 = std::min(out.r0, r);
                out.r1 = std::max(out.r1, r);
            }
        }
    }
    return out;
}

// `b` grown by the shapes lying wholly inside it grown by `e` samples (clipped to the grid).
Box gather(const AlphaGrid& g, WheelScratch& s, std::uint32_t wheel, const Box& b, std::uint32_t e) {
    const std::uint32_t c0 = b.c0 > e ? b.c0 - e : 0;
    const std::uint32_t r0 = b.r0 > e ? b.r0 - e : 0;
    const std::uint32_t c1 = std::min(b.c1 + e, g.columns - 1);
    const std::uint32_t r1 = std::min(b.r1 + e, g.rows - 1);
    Box out = b;
    for (std::uint32_t r = r0; r <= r1; ++r) {
        for (std::uint32_t c = c0; c <= c1; ++c) {
            const std::size_t at = std::size_t{r} * g.columns + c;
            if (s.label[at] != 0 || g.alpha[at] < kWheelMinAlpha) {
                continue;
            }
            const auto label = static_cast<std::uint32_t>(s.boxes.size());
            const Box p = flood(g, c, r, label, s);
            s.boxes.push_back(p);
            if (label != wheel && p.c0 >= c0 && p.r0 >= r0 && p.c1 <= c1 && p.r1 <= r1) {
                out.c0 = std::min(out.c0, p.c0);
                out.c1 = std::max(out.c1, p.c1);
                out.r0 = std::min(out.r0, p.r0);
                out.r1 = std::max(out.r1, p.r1);
            }
        }
    }
    return out;
}

} // namespace

PixelRect wheelSearchRect(std::uint32_t width, std::uint32_t height) {
    const PixelRect band = wideContentRect(width, height);
    const auto from =
        static_cast<std::int32_t>(std::floor(static_cast<double>(band.width) * kWheelSearchFrom));
    return PixelRect{band.x + from, band.y, band.width - static_cast<std::uint32_t>(from), band.height};
}

std::uint32_t gridColumns(const PixelRect& area, std::uint32_t step) {
    return step == 0 ? 0 : (area.width + step - 1) / step;
}

std::uint32_t gridRows(const PixelRect& area, std::uint32_t step) {
    return step == 0 ? 0 : (area.height + step - 1) / step;
}

std::optional<PixelRect>
measureWheel(const AlphaGrid& grid, std::uint32_t width, std::uint32_t height, WheelScratch* scratch) {
    const PixelRect band = wideContentRect(width, height);
    const PixelRect area = wheelSearchRect(width, height);
    if (grid.area.x != area.x || grid.area.y != area.y || grid.area.width != area.width ||
        grid.area.height != area.height || grid.step == 0 || grid.columns == 0 || grid.rows == 0 ||
        band.width == 0 || grid.alpha.size() != std::size_t{grid.columns} * grid.rows ||
        grid.columns != gridColumns(grid.area, grid.step) || grid.rows != gridRows(grid.area, grid.step)) {
        return std::nullopt;
    }
    WheelScratch local;
    WheelScratch& s = scratch ? *scratch : local;
    s.label.assign(grid.alpha.size(), 0);
    s.boxes.assign(1, Box{}); // label 0: not labelled
    const auto bandW = static_cast<double>(band.width);
    // Sample coordinates of a band point.
    const auto column = [&](float u) {
        return std::floor((band.x + u * bandW - grid.area.x) / grid.step);
    };
    const double row =
        std::floor((band.y + kWheelCentreRow * static_cast<double>(band.height) - grid.area.y) / grid.step);
    if (row < 0.0 || row >= grid.rows) {
        return std::nullopt;
    }
    const auto r = static_cast<std::uint32_t>(row);
    const auto from = static_cast<std::int64_t>(std::min<double>(column(kSeedFrom), grid.columns - 1.0));
    const auto to = static_cast<std::int64_t>(std::max(column(kSeedTo), 0.0));
    for (std::int64_t cs = from; cs >= to; --cs) {
        const auto c = static_cast<std::uint32_t>(cs);
        const std::size_t at = std::size_t{r} * grid.columns + c;
        if (s.label[at] != 0 || grid.alpha[at] < kWheelMinAlpha) {
            continue;
        }
        const auto label = static_cast<std::uint32_t>(s.boxes.size());
        Box b = flood(grid, c, r, label, s);
        s.boxes.push_back(b);
        // Reaching the search rectangle's top or bottom: a full-screen layer (a menu, a fade), not a wheel.
        if (b.r0 == 0 || b.r1 + 1 == grid.rows) {
            continue;
        }
        // Wider than round: what touches it on the left is cut off where the round wheel, as wide as it is
        // tall from its right edge, begins.
        const std::uint32_t tall = b.r1 - b.r0 + 1;
        if (b.c1 - b.c0 + 1 > tall * kMergedAspect) {
            b = clipLeft(grid, s, label, b, b.c1 + 1 > tall ? b.c1 + 1 - tall : 0);
        }
        const double w = static_cast<double>(b.c1 - b.c0 + 1) * grid.step;
        const double h = static_cast<double>(b.r1 - b.r0 + 1) * grid.step;
        const bool wheel = b.c0 > 0 && w >= kMinAcross * bandW && w <= kMaxAcross * bandW &&
                           h >= kMinAcross * bandW && h <= kMaxAcross * bandW && w <= h * kMaxAspect &&
                           h <= w * kMaxAspect;
        if (!wheel) {
            continue;
        }
        b = gather(grid, s, label, b,
                   static_cast<std::uint32_t>(std::ceil(kGatherFraction * (b.r1 - b.r0 + 1))));
        // A drawn pixel can lie up to a step short of the next sample; then the margin.
        const std::int64_t grow = std::int64_t{grid.step} + std::max<std::int64_t>(1, band.width / 100);
        const std::int64_t x0 =
            std::max<std::int64_t>(grid.area.x + std::int64_t{b.c0} * grid.step - grow, band.x);
        const std::int64_t y0 =
            std::max<std::int64_t>(grid.area.y + std::int64_t{b.r0} * grid.step - grow, band.y);
        const std::int64_t x1 = std::min<std::int64_t>(grid.area.x + std::int64_t{b.c1} * grid.step + grow,
                                                       std::int64_t{band.x} + band.width);
        const std::int64_t y1 = std::min<std::int64_t>(grid.area.y + std::int64_t{b.r1} * grid.step + grow,
                                                       std::int64_t{band.y} + band.height);
        if (x1 <= x0 || y1 <= y0) {
            return std::nullopt;
        }
        return PixelRect{static_cast<std::int32_t>(x0), static_cast<std::int32_t>(y0),
                         static_cast<std::uint32_t>(x1 - x0), static_cast<std::uint32_t>(y1 - y0)};
    }
    return std::nullopt;
}

PixelRect unionRect(const PixelRect& a, const PixelRect& b) {
    if (a.width == 0 || a.height == 0) {
        return b;
    }
    if (b.width == 0 || b.height == 0) {
        return a;
    }
    const std::int64_t x0 = std::min(a.x, b.x);
    const std::int64_t y0 = std::min(a.y, b.y);
    const std::int64_t x1 = std::max<std::int64_t>(std::int64_t{a.x} + a.width, std::int64_t{b.x} + b.width);
    const std::int64_t y1 =
        std::max<std::int64_t>(std::int64_t{a.y} + a.height, std::int64_t{b.y} + b.height);
    return PixelRect{static_cast<std::int32_t>(x0), static_cast<std::int32_t>(y0),
                     static_cast<std::uint32_t>(x1 - x0), static_cast<std::uint32_t>(y1 - y0)};
}

std::optional<PixelRect> WheelRectTracker::update(bool centred,
                                                  std::uint32_t width,
                                                  std::uint32_t height,
                                                  const WheelMeasurement& latest) {
    if (!centred) {
        if (centred_) {
            // Back on the HUD: what was measured belongs to this time up; its rectangle starts the next.
            seen_ = std::max(seen_, latest.serial);
            if (rect_) {
                last_ = rect_;
                lastWidth_ = rectWidth_;
                lastHeight_ = rectHeight_;
            }
            rect_.reset();
        }
        centred_ = false;
        return std::nullopt;
    }
    if (!centred_) {
        centred_ = true;
        fresh_.reset();
        smaller_ = 0;
        rect_.reset();
        carried_ = false;
        if (last_ && lastWidth_ == width && lastHeight_ == height) {
            rect_ = last_;
            carried_ = true;
        }
        rectWidth_ = width;
        rectHeight_ = height;
    }
    if (rectWidth_ != width || rectHeight_ != height) {
        // The target was resized while the wheel is up: a rectangle of the old size does not fit it.
        rect_.reset();
        fresh_.reset();
        carried_ = false;
        smaller_ = 0;
        rectWidth_ = width;
        rectHeight_ = height;
    }
    if (latest.serial <= seen_) {
        return rect_;
    }
    seen_ = latest.serial;
    if (latest.width != width || latest.height != height || latest.rect.width == 0 ||
        latest.rect.height == 0) {
        return rect_;
    }
    fresh_ = unionRect(fresh_.value_or(PixelRect{}), latest.rect);
    if (!rect_ || !carried_) {
        rect_ = unionRect(rect_.value_or(PixelRect{}), latest.rect);
    } else if (closeRects(*rect_, *fresh_)) {
        rect_ = unionRect(*rect_, *fresh_);
        carried_ = false;
    } else if (!containsRect(*rect_, *fresh_) || ++smaller_ >= kWheelShrinkReadings) {
        // Larger or elsewhere at once, or smaller for that many readings: the HUD is laid out anew.
        rect_ = fresh_;
        carried_ = false;
    }
    return rect_;
}

bool WheelReadCadence::next(bool wheelUp) {
    if (!wheelUp) {
        up_ = false;
        same_ = 0;
        since_ = 0;
        last_.reset();
        return false;
    }
    if (!up_ || same_ < kWheelStableReadings) {
        up_ = true;
        since_ = 0;
        return true;
    }
    if (++since_ >= kWheelSlowEvery) {
        since_ = 0;
        return true;
    }
    return false;
}

void WheelReadCadence::measured(const std::optional<PixelRect>& rect) {
    if (!up_) {
        return; // a reading of a time up that is over
    }
    const bool same = rect && last_ && rect->x == last_->x && rect->y == last_->y &&
                      rect->width == last_->width && rect->height == last_->height;
    same_ = same ? same_ + 1 : (rect ? 1 : 0);
    last_ = rect;
}

bool containsRect(const PixelRect& outer, const PixelRect& inner) {
    return inner.x >= outer.x && inner.y >= outer.y &&
           std::int64_t{inner.x} + inner.width <= std::int64_t{outer.x} + outer.width &&
           std::int64_t{inner.y} + inner.height <= std::int64_t{outer.y} + outer.height;
}

bool closeRects(const PixelRect& kept, const PixelRect& measured) {
    const double tolerance = kWheelRectTolerance * measured.width;
    const auto near = [tolerance](std::int64_t a, std::int64_t b) {
        return static_cast<double>(a > b ? a - b : b - a) <= tolerance;
    };
    return near(kept.x, measured.x) && near(kept.y, measured.y) &&
           near(std::int64_t{kept.x} + kept.width, std::int64_t{measured.x} + measured.width) &&
           near(std::int64_t{kept.y} + kept.height, std::int64_t{measured.y} + measured.height);
}

} // namespace evr::ui_layer
