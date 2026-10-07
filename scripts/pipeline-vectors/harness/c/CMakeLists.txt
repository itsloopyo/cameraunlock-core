cmake_minimum_required(VERSION 3.20)
project(cameraunlock_conformance_harness_c LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

set(CAMERAUNLOCK_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(CAMERAUNLOCK_BUILD_C ON CACHE BOOL "" FORCE)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../../../../cpp ${CMAKE_BINARY_DIR}/cameraunlock)

# The harness sees the C interface as a host does: its headers and its DLL, nothing of core's C++.
add_executable(conformance_harness_c main.cpp)
target_link_libraries(conformance_harness_c PRIVATE cameraunlock_c_testing)
target_compile_options(conformance_harness_c PRIVATE /W4 /WX)
add_custom_command(TARGET conformance_harness_c POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:cameraunlock_c_testing> $<TARGET_FILE_DIR:conformance_harness_c>)
