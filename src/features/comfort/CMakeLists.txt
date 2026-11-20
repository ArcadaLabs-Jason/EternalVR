# Comfort options: the vignette for artificial motion (and for the game's launches and rides), how glory kills and
# pickup cameras are shown. No OpenXR or Windows here.
evr_add_module(evr_comfort
    SOURCES
        glory_kill.cpp
        launch_pulse.cpp
        pickup_camera.cpp
        ride.cpp
        vignette.cpp
    DEPENDS
        evr_common)
