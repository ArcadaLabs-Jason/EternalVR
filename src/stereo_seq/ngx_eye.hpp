#pragma once

// Per-eye DLSS (vkcore/taa_ngx.hpp): the render an NGX evaluation belongs to, and which evaluations reset.
//
// The game evaluates DLSS in the backend frame's AA pass, late in the frame's jobs. The tag in flight read
// there names the next render once the render thread's swap has moved the backend counter on (the race the
// exposure hook counts at the earlier render-view job). The render's own tag is the one its output selector
// picked the accumulation image for: the selector notes that image with the tag (NgxOutputBook) and the
// evaluation finds it again by its `Output` image. The per-eye hook's history resets (TaaResetPlanner and
// the alternate-eye ones) are noted per eye and game frame (NgxResetBook), so the evaluation of that eye's
// render raises NGX's `Reset`: eye L's game feature is reset with eye R's twin, as both eyes' TAA history
// is. No engine or NGX code here, so it is tested on every platform.

#include "stereo_seq/eye_tags.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace evr::stereo_seq {

// The accumulation images the output selector picked last, with the tag it picked each for. Images are
// opaque values (VkImage handles); the oldest is forgotten when a new one does not fit (eye R's slot is
// rebuilt with new images when the engine resizes its own).
class NgxOutputBook {
public:
    static constexpr std::size_t kCapacity = 8;

    // The output selector picked `image` for the render tagged `tag` (0: nothing noted).
    void note(std::uint64_t image, const RenderTag& tag);
    // The tag of the render that picked `image` last; nullopt for 0 or an image not noted.
    std::optional<RenderTag> find(std::uint64_t image) const;

private:
    struct Entry {
        std::uint64_t image = 0;
        std::uint64_t order = 0; // when it was noted last (0: free)
        RenderTag tag;
    };
    std::array<Entry, kCapacity> entries_{};
    std::uint64_t notes_ = 0;
};

struct NgxEyePick {
    std::optional<RenderTag> tag; // what the evaluation goes by (nullopt: untagged, the game's feature)
    bool own = false;             // the render's own tag, found by its output image
    bool fallback = false;        // no own tag: the tag in flight, which names eye L or eye R
    bool inFlightDiffers = false; // own, and the tag in flight names the other eye (mono counts as eye L)
};

// `own`: the output image's tag (NgxOutputBook::find); `inFlight`: the tag in flight at the evaluation.
// The own tag goes unless the tag in flight shows it is stale: its present is neither the in-flight one nor
// the one before it (the swap moves the counter on by one at most while the frame's jobs run). Without an
// own tag, the tag in flight.
NgxEyePick pickNgxEye(const std::optional<RenderTag>& own, const std::optional<RenderTag>& inFlight);

// The history resets the per-eye hook wrote, per eye and game frame, until that eye's evaluation for that
// game frame takes it. The newest kCapacity are kept.
class NgxResetBook {
public:
    static constexpr std::size_t kCapacity = 8;

    // `eye`'s render of game frame `gameFrame` had its history reset.
    void note(Eye eye, std::uint64_t gameFrame);
    // `eye`'s evaluation for `gameFrame`: true, once, when that render was reset.
    bool take(Eye eye, std::uint64_t gameFrame);

private:
    struct Entry {
        Eye eye = Eye::Mono;
        std::uint64_t gameFrame = 0; // 0: free
    };
    std::array<Entry, kCapacity> entries_{};
    std::size_t next_ = 0;
};

} // namespace evr::stereo_seq
