#pragma once

// One placement of the arms model per game frame, for the arms and the gun (vkcore/frame_placement.hpp): the
// rule and the slot, without game memory.
//
// The off-hand hook poses the arms in the model's space under a placement of the model. When it runs in a
// frame before the viewmodel hook has placed the model in it, the frame's own placement is the one still to
// be written: it takes it itself (`placesAhead`) and pins it for the viewmodel hook, which writes the pinned
// one in that frame. (The game draws the arm a frame later, under the next frame's placement:
// drawn_wrist.hpp.)
//
// Pure: no game memory, no lock (the caller holds one).

#include "common/vector.hpp"
#include "xr_math/weapon_pose.hpp"

#include <cstdint>
#include <optional>
#include <string>

namespace evr::arm {

// Whether the off-hand hook, in game frame `frame`, poses under a placement of its own for that frame: the
// hands were last placed in another frame (`placedIn`, idHands' own once-a-frame check). False when either is
// unknown: then nothing is pinned, and the viewmodel hook places the model as it reads it.
bool placesAhead(std::optional<std::int32_t> frame, std::optional<std::int32_t> placedIn);

// The pinned placement of one game frame for one set of hands.
template <typename T>
class FramePin {
public:
    void pin(const void* hands, std::int32_t frame, const T& value) { pin_ = Slot{hands, frame, value}; }
    void clear() { pin_.reset(); }
    // The value pinned for `hands` in `frame`, taken: the slot is empty afterwards whatever it held.
    std::optional<T> take(const void* hands, std::optional<std::int32_t> frame) {
        std::optional<T> out;
        if (pin_ && frame && pin_->hands == hands && pin_->frame == *frame) {
            out = pin_->value;
        }
        pin_.reset();
        return out;
    }

private:
    struct Slot {
        const void* hands = nullptr;
        std::int32_t frame = 0;
        T value;
    };
    std::optional<Slot> pin_;
};

// The viewmodel hook places the model itself every frame (the gun is never placed by the pin); it checks the
// frame's pin against its own placement. What the arm was posed under in a frame against what the model got.
// The inputs a placement was made from: the held item, the hands snapshot's stamp and, with the weapon in the
// left hand, the left glove's placement on the gun (its item, weight and wrist).
struct PinInputs {
    const void* item = nullptr;
    std::int64_t worldStamp = 0;
    const void* leftItem = nullptr;
    float leftWeight = 0.0f;
    Vec3 leftWrist;
};

enum class PinCheck : std::uint8_t {
    Same,      // the model got the placement the arm was posed under
    Item,      // it differed, the held item not the one the pin was made for (a switch, a mod, the fists)
    Hands,     // it differed, from other hands (a camera hook between the two hooks)
    LeftGlove, // it differed, the left glove's placement on the gun changed in between
    Placement, // it differed, the inputs the same (not expected)
    NotPlaced, // the viewmodel hook did not place the model in that frame
};

// Within 0.01 mm and 1e-5 per axis component: the same placement.
bool samePlacement(const xr_math::EyeRelativePose& a, const xr_math::EyeRelativePose& b);

// The check of a pin made from `pinned` with `pinnedPlacement`, the viewmodel hook's own made from `own`.
PinCheck checkPin(const PinInputs& pinned,
                  const xr_math::EyeRelativePose& pinnedPlacement,
                  const PinInputs& own,
                  const xr_math::EyeRelativePose& ownPlacement);

const char* pinCheckName(PinCheck check);

// The checks of 10 s, for the log ("frame placement: last 10 s: ...").
class PinTally {
public:
    void note(PinCheck check);
    [[nodiscard]] bool empty() const { return total_ == 0; }
    [[nodiscard]] std::string line() const;
    void reset() { *this = PinTally{}; }

private:
    std::uint64_t counts_[6] = {};
    std::uint64_t total_ = 0;
};

} // namespace evr::arm
