# Comfort options: the vignette for artificial motion, how glory kills and pickup cameras are shown. No OpenXR or
# Windows here.
evr_add_module(evr_comfort
    SOURCES
        glory_kill.cpp
        pickup_camera.cpp
        vignette.cpp
    DEPENDS
        evr_common)
