cmake_minimum_required(VERSION 3.20)
project(cameraunlock_rig_guard LANGUAGES C CXX)

# The guarded launch of a background test's game (docs/isolated-input.md): the
# guard, the shim it puts in the game, and the misbehaving stand-in its test runs.
# Built once for x64 and once for Win32, into one folder, where each file finds
# the others by name.

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
# Static CRT: the shim is loaded into games that ship no runtime of ours.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(RIG_ARCH x64)
else()
    set(RIG_ARCH x86)
endif()
set(RIG_OUT "${CMAKE_CURRENT_SOURCE_DIR}/build/out")

add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../../../vendor/minhook ${CMAKE_BINARY_DIR}/minhook)

add_library(rig_shim SHARED shim.cpp)
target_link_libraries(rig_shim PRIVATE minhook)
set_target_properties(rig_shim PROPERTIES OUTPUT_NAME "CameraUnlockRigShim.${RIG_ARCH}" PREFIX "")

# No console of its own: a console program started from a background script opens a window.
add_executable(rig_guard WIN32 rig.cpp)
target_link_options(rig_guard PRIVATE /ENTRY:wmainCRTStartup)
set_target_properties(rig_guard PROPERTIES OUTPUT_NAME "CameraUnlockRig.${RIG_ARCH}")

add_executable(rig_misbehave WIN32 misbehave.cpp)
target_link_options(rig_misbehave PRIVATE /ENTRY:wmainCRTStartup)
target_link_libraries(rig_misbehave PRIVATE d3d9 d3d11 dxgi dinput8 dxguid)
set_target_properties(rig_misbehave PROPERTIES OUTPUT_NAME "RigMisbehave.${RIG_ARCH}")

foreach(target rig_shim rig_guard rig_misbehave)
    target_compile_definitions(${target} PRIVATE NOMINMAX UNICODE _UNICODE _CRT_SECURE_NO_WARNINGS)
    target_compile_options(${target} PRIVATE /W4 /WX)
    set_target_properties(${target} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY_RELEASE "${RIG_OUT}"
        LIBRARY_OUTPUT_DIRECTORY_RELEASE "${RIG_OUT}"
        RUNTIME_OUTPUT_DIRECTORY_DEBUG "${RIG_OUT}/debug"
        LIBRARY_OUTPUT_DIRECTORY_DEBUG "${RIG_OUT}/debug")
endforeach()
