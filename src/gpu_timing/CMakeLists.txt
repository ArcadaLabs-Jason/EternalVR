# GPU timing (docs/VR_STEREO.md, "GPU timing"): the logic that needs no Vulkan (query ring, timestamp
# arithmetic, per-frame GPU time, the 10 s summaries, which present gaps are stalls), so it is tested on
# every platform.
evr_add_module(evr_gpu_timing
    SOURCES
        cpu_split.cpp
        frame_gpu.cpp
        present_stall.cpp
        query_ring.cpp
        stats.cpp
        submit_census.cpp
        timestamps.cpp
        timing_window.cpp)
