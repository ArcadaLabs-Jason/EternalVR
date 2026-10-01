# Frame pacing (ETERNALVR_PACE): the game held to one image per headset frame, its wait and timeout, and the
# cadence counters. No OpenXR or Windows here.
evr_add_module(evr_pacing
    SOURCES
        pace_policy.cpp)
