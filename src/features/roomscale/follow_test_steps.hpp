#pragma once

// The usercmd movement precision test (T-062, docs/VR_ROOMSCALE.md "Body follow"): scripted head steps
// that body follow has to walk the body after, for a runtime whose head never moves by itself
// (OpenXR-Simulator).
//
// ETERNALVR_TEST_STEPS lists step sizes in metres. Each step is two legs: the fake head offset moves
// out by the step along the axis (forward or right), then back to zero, each leg held for the hold time;
// the list repeats. The probe measures, per leg, how far the body really moved along the axis and across
// it, how far it overshot and how long it took to cover 90 % of the step, against what was asked.
//
// ETERNALVR_TEST_MOVE instead lists move command values (1 to 127): each is sent as a constant command
// along the axis for one hold, then nothing for one hold, and the probe measures the speed it gives (the
// command-to-speed curve), when the body first moved and how far it coasted after.

#include "common/vector.hpp"

#include <vector>

namespace evr::roomscale {

struct TestSteps {
    std::vector<float> metres;
    float holdSeconds = 3.0f;
    bool sideways = false; // the right axis (+X) instead of forward (-Z)
    bool commands = false; // `metres` are move command values (ETERNALVR_TEST_MOVE), not head steps
};

// The step axis in room space.
Vec3 testStepAxis(const TestSteps& steps);

struct TestStepPhase {
    int leg = -1;         // -1 before the first leg; counts up through every repeat
    int legs = 0;         // legs in one pass of the list
    Vec3 offset;          // the fake head offset now (room metres)
    float asked = 0.0f;   // this leg's change of the offset along the axis
    int command = 0;      // commands: the move value this leg sends (0 on the stop legs)
    double started = 0.0; // when this leg started (seconds since the steps started)
};

// The phase `seconds` after the steps started (negative: before).
TestStepPhase testStepAt(const TestSteps& steps, double seconds);

struct StepReport {
    int leg = -1;
    float asked = 0.0f;         // metres along the axis
    float along = 0.0f;         // the body's move along the axis (signed, same sense as asked)
    float across = 0.0f;        // the largest distance off the axis
    float peak = 0.0f;          // the furthest the body got along the asked sense
    float absorbed = 0.0f;      // what body follow took into the anchor along the axis
    double reachSeconds = -1.0; // time to 90 % of the step; -1 if never
    double seconds = 0.0;       // the leg's length
    double firstMotion = -1.0;  // time until the body had moved 2 mm; -1 if never
    float speed = 0.0f;         // metres per second along the axis over the second half of the leg
    float gapStart = -1.0f;     // the head's gap from the body when the leg started (-1: not given)
    float gapLeft = -1.0f;      // and at its end
    double closeSeconds = -1.0; // time until the gap was within kCloseMetres; -1 if never
};

// The step test's criterion: the gap closed to within this.
inline constexpr float kCloseMetres = 0.02f;

class StepProbe {
public:
    // `holdSeconds` places the half-way mark the steady speed is measured from.
    void begin(int leg, float asked, Vec3 axis, double seconds, double holdSeconds = 0.0);
    // One frame: the body's displacement and the anchor shift, both room metres, and the gap left
    // between the head and the body (negative: not known).
    void add(Vec3 displacement, Vec3 absorbed, double seconds, float gap = -1.0f);
    [[nodiscard]] bool active() const { return active_; }
    [[nodiscard]] StepReport report() const;

private:
    bool active_ = false;
    Vec3 axis_{0.0f, 0.0f, -1.0f};
    Vec3 moved_;
    double start_ = 0.0;
    double last_ = 0.0;
    double half_ = 0.0;
    bool halfTaken_ = false;
    float halfAlong_ = 0.0f;
    double halfSeconds_ = 0.0;
    StepReport report_;
};

} // namespace evr::roomscale
