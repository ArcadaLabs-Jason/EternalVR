# Stand-in for the game in the rig script tests (tools/rig/tests). Windows only.
add_executable(evr_rig_testapp WIN32 main.cpp)
target_compile_definitions(evr_rig_testapp PRIVATE UNICODE _UNICODE)
target_link_libraries(evr_rig_testapp PRIVATE evr_warnings winmm ole32)
