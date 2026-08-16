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
