# Tracking values checked before use: the headset's FOV over a session (fov_watch.hpp), tracked positions that
# jumped (pose_guard.hpp). No OpenXR or Windows here.
evr_add_module(evr_tracking
    SOURCES
        fov_watch.cpp
        pose_guard.cpp
    DEPENDS
        evr_common
        evr_xr_math)
