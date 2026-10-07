cmake_minimum_required(VERSION 3.20)

add_executable(cameraunlock_dxbc_tests dxbc_tests.cpp)
target_link_libraries(cameraunlock_dxbc_tests PRIVATE cameraunlock)
add_test(NAME cameraunlock_dxbc_tests COMMAND cameraunlock_dxbc_tests)

# Simple test executable
add_executable(cameraunlock_tests
    test_main.cpp
    ads_tests.cpp
    math_tests.cpp
    protocol_tests.cpp
    receiver_tests.cpp
    pose_jump_gate_tests.cpp
    util_tests.cpp
    session_tests.cpp
    smoothing_tests.cpp
    unreal_math_tests.cpp
    projection_tests.cpp
    re_math_tests.cpp
    interpolator_tests.cpp
    probe_selection_tests.cpp
    file_log_tests.cpp
    hotkey_tests.cpp
    os_tests.cpp
    value_guard_tests.cpp
    frozen_ini_helper_tests.cpp
    win32_profile_semantics_tests.cpp
    config_schema_tests.cpp
    safe_memory_tests.cpp
    lean_clamp_tests.cpp
    held_pointer_tests.cpp
    input_script_tests.cpp
    directinput_state_tests.cpp
    lean_line_sweep_tests.cpp
    rig_lean_tests.cpp
    zoom_compensation_tests.cpp
    tracking_mode_tests.cpp
    ini_editor_tests.cpp
    checked_file_writer_tests.cpp
    canonical_ini_tests.cpp
    key_bindings_tests.cpp
    value_codecs_tests.cpp
    config_table_tests.cpp
    head_tracking_config_table_tests.cpp
    defaults_ini_tests.cpp
    defaults_location_tests.cpp
    preferences_fixture_tests.cpp
    legacy_import_tests.cpp
    ini_mutations_tests.cpp
    config_owner_tests.cpp
    canonical_config_example_tests.cpp
    getasynckeystate_probe.cpp
    defaults_ini_probe.cpp
)

target_link_libraries(cameraunlock_tests PRIVATE cameraunlock)
if(WIN32)
    target_link_libraries(cameraunlock_tests PRIVATE bcrypt)
endif()
target_compile_definitions(cameraunlock_tests PRIVATE
    CAMERAUNLOCK_INI_EDITOR_FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/../../data/fixtures/canonical-ini/editor"
    CAMERAUNLOCK_CANONICAL_INI_FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/../../data/fixtures/canonical-ini"
    CAMERAUNLOCK_POSE_JUMP_GATE_FIXTURE="${CMAKE_CURRENT_SOURCE_DIR}/../../data/fixtures/pose-jump-gate/cases.tsv")

# plugin_config.cpp lives in the optional REFramework target, so its tests can
# only build when that target is configured.
if(CAMERAUNLOCK_BUILD_REFRAMEWORK)
    target_sources(cameraunlock_tests PRIVATE plugin_config_migration_tests.cpp plugin_config_canonical_tests.cpp)
    target_link_libraries(cameraunlock_tests PRIVATE cameraunlock_reframework)
    target_compile_definitions(cameraunlock_tests PRIVATE CAMERAUNLOCK_TESTS_REFRAMEWORK
        CAMERAUNLOCK_REFRAMEWORK_LEGACY_FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/../../data/fixtures/reframework-legacy")
endif()

add_test(NAME cameraunlock_tests COMMAND cameraunlock_tests)

# The C interface, driven as a host drives it: through CameraUnlockCore.dll and the C header
# alone. c_header.c compiles that header as C.
if(CAMERAUNLOCK_BUILD_C)
    enable_language(C)
    add_executable(cameraunlock_c_tests c_interface_tests.cpp c_header.c)
    target_link_libraries(cameraunlock_c_tests PRIVATE cameraunlock_c ws2_32)
    target_compile_options(cameraunlock_c_tests PRIVATE /W4 /WX)
    add_custom_command(TARGET cameraunlock_c_tests POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:cameraunlock_c> $<TARGET_FILE_DIR:cameraunlock_c_tests>)
    add_test(NAME cameraunlock_c_tests COMMAND cameraunlock_c_tests)
endif()

# Compile-only, and STRICT. Every public header in one TU at the warning level the
# fleet's own mods use, because a problem that only appears when two headers meet is
# invisible to any build that includes them separately - which is every build here.
# `/WX` is deliberately NOT put on the `cameraunlock` library target: mods build the
# core from source via add_subdirectory, and forcing warnings-as-errors on them would
# turn any future compiler's new warning into a fleet-wide outage. This target carries
# the strictness instead. See cpp/tests/headers_strict.cpp for what it caught.
add_library(cameraunlock_headers_strict OBJECT headers_strict.cpp)
target_include_directories(cameraunlock_headers_strict PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../include)
if(MSVC)
    target_compile_options(cameraunlock_headers_strict PRIVATE /W4 /WX)
else()
    # -Wshadow is deliberately absent: gcc flags a constructor parameter shadowing a
    # member, which is idiomatic here and is NOT what MSVC's C4459 reports.
    target_compile_options(cameraunlock_headers_strict PRIVATE -Wall -Wextra -Werror)
endif()

# Compile-only. The overlay headers are header-only templates whose bodies live
# behind CAMERAUNLOCK_DX*_OVERLAY_IMPLEMENTATION, so until this target existed
# nothing in this repo ever expanded them and a syntax or type error only
# surfaced in a consuming mod. Nothing is linked or run - see
# overlay_stubs/README.md.
if(WIN32)
    add_executable(cameraunlock_dx9_device_lifetime_tests dx9_device_lifetime_tests.cpp)
    target_include_directories(cameraunlock_dx9_device_lifetime_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/overlay_stubs
        ${CMAKE_CURRENT_SOURCE_DIR}/../include
    )
    target_link_libraries(cameraunlock_dx9_device_lifetime_tests PRIVATE d3d9)
    add_test(NAME cameraunlock_dx9_device_lifetime_tests COMMAND cameraunlock_dx9_device_lifetime_tests)

    add_library(cameraunlock_overlay_compile OBJECT
        overlay_stubs/overlay_compile_dx9.cpp
        overlay_stubs/overlay_compile_dx11.cpp
        overlay_stubs/overlay_compile_dx12.cpp
        overlay_stubs/overlay_compile_aim_marker_dx9.cpp
        overlay_stubs/overlay_compile_aim_marker_dx11.cpp
        overlay_stubs/overlay_compile_aim_marker_dx12.cpp
    )
    target_include_directories(cameraunlock_overlay_compile PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/overlay_stubs
        ${CMAKE_CURRENT_SOURCE_DIR}/../include
    )
endif()
