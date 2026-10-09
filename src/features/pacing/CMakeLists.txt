# Frame pacing (ETERNALVR_PACE): the game held to one image per headset frame, its wait and timeout, and the
# cadence counters; the headset's display period over a session (refresh changes, throttling); the cap behind a
# runtime's menu, the runtime's frame clock and the copy wait (frame_clock_watch.hpp), how often a stalled clock
# restarts the session (restart_budget.hpp), how far ahead the head is predicted (prediction_period.hpp),
# which image the XR worker shows (slot_choice.hpp). No OpenXR or Windows here.
evr_add_module(evr_pacing
    SOURCES
        display_period_watch.cpp
        frame_clock_watch.cpp
        pace_policy.cpp
        prediction_period.cpp
        restart_budget.cpp
        slot_choice.cpp)
