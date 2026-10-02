# Frame pacing (ETERNALVR_PACE): the game held to one image per headset frame, its wait and timeout, and the
# cadence counters; the headset's display period over a session (refresh changes, throttling). No OpenXR or
# Windows here.
evr_add_module(evr_pacing
    SOURCES
        display_period_watch.cpp
        pace_policy.cpp)
