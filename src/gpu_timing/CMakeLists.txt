# GPU timing (docs/VR_STEREO.md, "GPU timing"): the logic that needs no Vulkan (query ring, timestamp
# arithmetic, per-frame GPU time, the 10 s summaries), so it is tested on every platform.
evr_add_module(evr_gpu_timing
    SOURCES
        cpu_split.cpp
        frame_gpu.cpp
        query_ring.cpp
        stats.cpp
        timestamps.cpp
        timing_window.cpp)
