evr_add_module(evr_posture
    SOURCES
        anchor_detector.cpp
        eye_height.cpp
        posture_detector.cpp
        posture_tracker.cpp
    DEPENDS
        evr_common)
