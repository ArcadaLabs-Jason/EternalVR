# Synchronized sequential stereo (Route S, docs/VR_STEREO.md): the logic that needs no engine, Vulkan or
# OpenXR (eye tags, present pairing, per-eye previous matrices, capture images, settings), so it is
# tested on every platform.
evr_add_module(evr_stereo_seq
    SOURCES
        bin_tiles.cpp
        centered_matrix.cpp
        desktop_window.cpp
        eye_pairing.cpp
        eye_tags.cpp
        eye_view_plan.cpp
        keep_prev.cpp
        moved_flag.cpp
        object_prev.cpp
        png_writer.cpp
        prev_matrices.cpp
        render_idle.cpp
        scatter_history.cpp
        seq_settings.cpp
        stack_budget.cpp
        stereo_taa.cpp
        world_gui.cpp)
