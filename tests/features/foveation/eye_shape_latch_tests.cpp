#include "features/foveation/eye_shape_latch.hpp"

#include <doctest/doctest.h>

#include <ostream>

using evr::Quat;
using evr::Vec3;
using evr::foveation::EyeShape;
using evr::foveation::EyeShapeLatch;
using Note = evr::foveation::EyeShapeLatch::Note;

namespace {

constexpr float kRadians = 1.0f / 57.29578f;

EyeShape shape(float left, float right, float up, float down, float yawDegrees = 0.0f) {
    return {{left * kRadians, right * kRadians, up * kRadians, down * kRadians},
            Quat::fromAxisAngle(Vec3{0.0f, 1.0f, 0.0f}, yawDegrees * kRadians)};
}

const EyeShape kQuest3 = shape(-54.0f, 40.0f, 44.0f, -55.0f);
const EyeShape kWider = shape(-56.0f, 42.0f, 46.0f, -56.0f);

// Notes `shape` `n` times; the last note.
Note noteTimes(EyeShapeLatch& latch, const EyeShape& s, int n) {
    Note last = Note::Same;
    for (int i = 0; i < n; ++i) {
        last = latch.note(s);
    }
    return last;
}

} // namespace

TEST_CASE("shapes within the tolerance are the same; a FOV angle or the orientation off is not") {
    CHECK(sameShape(kQuest3, shape(-54.5f, 40.5f, 43.5f, -55.5f, 0.5f), 1.0f));
    CHECK_FALSE(sameShape(kQuest3, shape(-55.5f, 40.0f, 44.0f, -55.0f), 1.0f));
    CHECK_FALSE(sameShape(kQuest3, shape(-54.0f, 40.0f, 44.0f, -55.0f, 2.0f), 1.0f));
}

TEST_CASE("the first shape is taken at once and the same one stays") {
    EyeShapeLatch latch;
    CHECK(latch.note(kQuest3) == Note::First);
    CHECK(latch.note(shape(-54.2f, 40.1f, 44.0f, -55.0f)) == Note::Same);
    REQUIRE(latch.shape());
    CHECK(sameShape(*latch.shape(), kQuest3, 0.01f));
}

TEST_CASE("a different shape is taken once it holds; one that comes and goes never") {
    EyeShapeLatch latch;
    latch.note(kQuest3);
    CHECK(noteTimes(latch, kWider, EyeShapeLatch::kStableNotes - 1) == Note::Waiting);
    CHECK(latch.note(kQuest3) == Note::Same); // back: the wait starts over
    CHECK(noteTimes(latch, kWider, EyeShapeLatch::kStableNotes - 1) == Note::Waiting);
    CHECK(sameShape(*latch.shape(), kQuest3, 0.01f));
    CHECK(latch.note(kWider) == Note::Changed);
    CHECK(sameShape(*latch.shape(), kWider, 0.01f));
    CHECK(latch.changes() == 1);
}

TEST_CASE("changes stop at the limit; the shape then stays") {
    EyeShapeLatch latch;
    latch.note(kQuest3);
    for (int i = 0; i < EyeShapeLatch::kMaxChanges; ++i) {
        CHECK(noteTimes(latch, i % 2 == 0 ? kWider : kQuest3, EyeShapeLatch::kStableNotes) == Note::Changed);
    }
    const EyeShape kept = *latch.shape();
    const EyeShape other = sameShape(kept, kWider, 0.01f) ? kQuest3 : kWider;
    CHECK(noteTimes(latch, other, EyeShapeLatch::kStableNotes) == Note::Capped);
    CHECK(sameShape(*latch.shape(), kept, 0.01f));
    CHECK(latch.changes() == EyeShapeLatch::kMaxChanges);
}
