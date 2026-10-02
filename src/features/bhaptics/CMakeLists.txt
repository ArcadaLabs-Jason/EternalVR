# bHaptics suits and sleeves (docs/BHAPTICS.md): the effects and the Player's messages. No Windows or network
# here; the layer owns the connection.
evr_add_module(evr_bhaptics
    SOURCES
        bhaptics_message.cpp
        body_haptics.cpp
        crystal.cpp
        landing.cpp
        launch.cpp
        pickup_frames.cpp
        pickups.cpp
    DEPENDS
        evr_common
        evr_input)
