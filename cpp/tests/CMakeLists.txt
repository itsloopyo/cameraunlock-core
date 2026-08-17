cmake_minimum_required(VERSION 3.20)

# Simple test executable
add_executable(cameraunlock_tests
    test_main.cpp
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
)

target_link_libraries(cameraunlock_tests PRIVATE cameraunlock)

add_test(NAME cameraunlock_tests COMMAND cameraunlock_tests)

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
    )
    target_include_directories(cameraunlock_overlay_compile PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/overlay_stubs
        ${CMAKE_CURRENT_SOURCE_DIR}/../include
    )
endif()
