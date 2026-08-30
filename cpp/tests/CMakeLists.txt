cmake_minimum_required(VERSION 3.20)

# Simple test executable
add_executable(cameraunlock_tests
    test_main.cpp
    ads_tests.cpp
    math_tests.cpp
    protocol_tests.cpp
    receiver_tests.cpp
    util_tests.cpp
    session_tests.cpp
    smoothing_tests.cpp
    unreal_math_tests.cpp
    projection_tests.cpp
    interpolator_tests.cpp
    probe_selection_tests.cpp
    file_log_tests.cpp
    hotkey_tests.cpp
    os_tests.cpp
    value_guard_tests.cpp
    config_schema_tests.cpp
    safe_memory_tests.cpp
)

target_link_libraries(cameraunlock_tests PRIVATE cameraunlock)

add_test(NAME cameraunlock_tests COMMAND cameraunlock_tests)

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
    add_library(cameraunlock_overlay_compile OBJECT
        overlay_stubs/overlay_compile_dx9.cpp
        overlay_stubs/overlay_compile_dx11.cpp
        overlay_stubs/overlay_compile_dx12.cpp
        overlay_stubs/overlay_compile_aim_marker_dx11.cpp
    )
    target_include_directories(cameraunlock_overlay_compile PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/overlay_stubs
        ${CMAKE_CURRENT_SOURCE_DIR}/../include
    )
endif()
