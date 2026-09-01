cmake_minimum_required(VERSION 3.20)
project(cameraunlock VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(CAMERAUNLOCK_BUILD_TESTS "Build unit tests" ON)

# Windows-specific settings
if(WIN32)
    add_definitions(-DWIN32_LEAN_AND_MEAN -DNOMINMAX -D_CRT_SECURE_NO_WARNINGS)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

# Library source files
set(CAMERAUNLOCK_SOURCES
    src/data/tracking_pose.cpp
    src/math/angle_utils.cpp
    src/math/deadzone_utils.cpp
    src/math/smoothing_utils.cpp
    src/protocol/opentrack_packet.cpp
    src/protocol/udp_socket.cpp
    src/protocol/udp_receiver.cpp
    src/protocol/polling_udp_receiver.cpp
    src/processing/center_offset_manager.cpp
    src/processing/tracking_processor.cpp
    src/config/ini_reader.cpp
    src/config/head_tracking_config.cpp
    src/config/value_guards.cpp
    src/os/module_paths.cpp
    src/os/game_window.cpp
    src/discovery/probe_selection.cpp
    src/memory/pattern_scanner.cpp
    src/memory/pe_fingerprint.cpp
    src/input/hotkey_poller.cpp
    src/logging/file_log.cpp
    src/diagnostics/crash_handler.cpp
)

# Create static library
add_library(cameraunlock STATIC ${CAMERAUNLOCK_SOURCES})

target_include_directories(cameraunlock
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

# Release optimization flags
if(MSVC)
    target_compile_options(cameraunlock PRIVATE
        $<$<CONFIG:Release>:/O2 /GL>
    )
    set_target_properties(cameraunlock PROPERTIES
        STATIC_LIBRARY_OPTIONS "$<$<CONFIG:Release>:/LTCG>"
    )
else()
    target_compile_options(cameraunlock PRIVATE
        $<$<CONFIG:Release>:-O3>
    )
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)
endif()

# Link Winsock for UDP on Windows
if(WIN32)
    target_link_libraries(cameraunlock PUBLIC ws2_32)
endif()

# Optional hook manager module (requires MinHook)
option(CAMERAUNLOCK_BUILD_HOOKS "Build hook manager (uses the vendored MinHook unless the consumer supplies one)" OFF)
if(CAMERAUNLOCK_BUILD_HOOKS)
    # A consumer that already defines a `minhook` target keeps it - several mods
    # add their own before this point and must not be broken. Everyone else gets
    # the copy vendored here. Before that copy existed, the fleet provisioned
    # MinHook five incompatible ways across three versions, half of them
    # FetchContent calls that hit the network on every clean configure including
    # the release job, and one pinned to a MUTABLE TAG.
    if(NOT TARGET minhook)
        set(CAMERAUNLOCK_MINHOOK_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../vendor/minhook")
        if(NOT EXISTS "${CAMERAUNLOCK_MINHOOK_DIR}/CMakeLists.txt")
            message(FATAL_ERROR "CAMERAUNLOCK_BUILD_HOOKS: no `minhook` target was supplied and the vendored copy is missing from ${CAMERAUNLOCK_MINHOOK_DIR}.")
        endif()
        # MinHook 1.3.4 declares `cmake_minimum_required(VERSION 3.0...3.5)`, and
        # CMake 4 removed compatibility with anything below 3.5. This is the
        # documented escape hatch, scoped to the add_subdirectory so it cannot
        # relax policies for the consumer's own tree.
        set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
        add_subdirectory("${CAMERAUNLOCK_MINHOOK_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/minhook")
        unset(CMAKE_POLICY_VERSION_MINIMUM)
    endif()

    add_library(cameraunlock_hooks STATIC src/hooks/hook_manager.cpp)
    target_include_directories(cameraunlock_hooks
        PUBLIC
            $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
            $<INSTALL_INTERFACE:include>
    )
    target_link_libraries(cameraunlock_hooks PUBLIC minhook)

    install(TARGETS cameraunlock_hooks
        ARCHIVE DESTINATION lib
        LIBRARY DESTINATION lib
    )
endif()

# Optional camera discovery module (requires MinHook via cameraunlock_hooks)
option(CAMERAUNLOCK_BUILD_DISCOVERY "Build camera discovery (requires MinHook)" OFF)
if(CAMERAUNLOCK_BUILD_DISCOVERY)
    if(NOT TARGET cameraunlock_hooks)
        message(FATAL_ERROR "CAMERAUNLOCK_BUILD_DISCOVERY requires CAMERAUNLOCK_BUILD_HOOKS=ON")
    endif()

    add_library(cameraunlock_discovery STATIC
        src/memory/rtti_vtable.cpp
        src/discovery/camera_discovery.cpp
        src/discovery/float_classifier.cpp
    )
    target_include_directories(cameraunlock_discovery
        PUBLIC
            $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
            $<INSTALL_INTERFACE:include>
    )
    target_link_libraries(cameraunlock_discovery PUBLIC cameraunlock cameraunlock_hooks)
    if(MSVC)
        target_compile_options(cameraunlock_discovery PRIVATE /EHa)
    endif()

    install(TARGETS cameraunlock_discovery
        ARCHIVE DESTINATION lib
        LIBRARY DESTINATION lib
    )
endif()

# Optional Unreal Engine runtime helpers (UE5 LWC math, GUObjectArray/FName
# reflection, SEH-guarded memory access). Windows-only.
option(CAMERAUNLOCK_BUILD_UNREAL "Build Unreal Engine runtime helpers" OFF)
if(CAMERAUNLOCK_BUILD_UNREAL)
    add_library(cameraunlock_unreal STATIC src/unreal/ue_runtime.cpp src/unreal/umg_reticle.cpp)
    target_include_directories(cameraunlock_unreal
        PUBLIC
            $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
            $<INSTALL_INTERFACE:include>
    )
    if(MSVC)
        target_compile_options(cameraunlock_unreal PRIVATE
            $<$<CONFIG:Release>:/O2 /GL>
        )
        set_target_properties(cameraunlock_unreal PROPERTIES
            STATIC_LIBRARY_OPTIONS "$<$<CONFIG:Release>:/LTCG>"
        )
    endif()

    install(TARGETS cameraunlock_unreal
        ARCHIVE DESTINATION lib
        LIBRARY DESTINATION lib
    )
endif()

# Optional REFramework utilities module (requires REFramework headers from consumer)
option(CAMERAUNLOCK_BUILD_REFRAMEWORK "Build REFramework utilities" OFF)
if(CAMERAUNLOCK_BUILD_REFRAMEWORK)
    add_library(cameraunlock_reframework STATIC
        src/reframework/log_callback.cpp
        src/reframework/managed_utils.cpp
        src/reframework/game_state_probing.cpp
        src/reframework/tdb_inspector.cpp
        src/reframework/camera_chain.cpp
        src/reframework/camera_controller_hook.cpp
        src/reframework/game_window.cpp
        src/reframework/gameplay_gate.cpp
        src/reframework/manager_probe_checks.cpp
        src/reframework/plugin_config.cpp
        src/reframework/plugin_mod.cpp
        src/reframework/camera_pipeline.cpp
        src/reframework/gui_elements.cpp
        src/reframework/plugin_bootstrap.cpp
    )
    # REFramework's API.hpp requires C++20 (std::span)
    set_target_properties(cameraunlock_reframework PROPERTIES
        CXX_STANDARD 20
        CXX_STANDARD_REQUIRED ON
    )
    target_include_directories(cameraunlock_reframework
        PUBLIC
            $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
            $<INSTALL_INTERFACE:include>
    )
    # REFramework's plugin SDK headers (reframework/API.hpp, reframework/API.h).
    # A consumer that already vendors its own points CAMERAUNLOCK_REFRAMEWORK_INCLUDE_DIR
    # at it and keeps that copy; everyone else gets the one vendored here, which is
    # praydog/REFramework plugin API 1.15.0 verbatim (MIT, vendor/reframework/LICENSE).
    #
    # Vendored rather than left to the consumer because until it was, nothing in this
    # repo could compile a single line of the RE Engine driver: the target could not
    # configure without headers it did not have, so `pixi run check` built none of it
    # and a compile error here reached the fleet.
    #
    # PUBLIC, not PRIVATE: the public headers under include/cameraunlock/reframework/
    # include <reframework/API.hpp> themselves, so a consumer of this target needs the
    # same include path to use them at all.
    if(NOT CAMERAUNLOCK_REFRAMEWORK_INCLUDE_DIR)
        set(CAMERAUNLOCK_REFRAMEWORK_INCLUDE_DIR
            "${CMAKE_CURRENT_SOURCE_DIR}/../vendor/reframework/include")
    endif()
    if(NOT EXISTS "${CAMERAUNLOCK_REFRAMEWORK_INCLUDE_DIR}/reframework/API.hpp")
        message(FATAL_ERROR "CAMERAUNLOCK_BUILD_REFRAMEWORK: no reframework/API.hpp under ${CAMERAUNLOCK_REFRAMEWORK_INCLUDE_DIR}.")
    endif()
    target_include_directories(cameraunlock_reframework PUBLIC
        ${CAMERAUNLOCK_REFRAMEWORK_INCLUDE_DIR}
    )

    # game_window.cpp forwards to cameraunlock::os::CenterGameWindowOnce, which
    # lives in the always-on target.
    target_link_libraries(cameraunlock_reframework PUBLIC cameraunlock)

    if(MSVC)
        target_compile_options(cameraunlock_reframework PRIVATE
            $<$<CONFIG:Release>:/O2 /GL>
        )
        set_target_properties(cameraunlock_reframework PROPERTIES
            STATIC_LIBRARY_OPTIONS "$<$<CONFIG:Release>:/LTCG>"
        )
    endif()

    install(TARGETS cameraunlock_reframework
        ARCHIVE DESTINATION lib
        LIBRARY DESTINATION lib
    )
endif()

# Tests
if(CAMERAUNLOCK_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# Install rules
install(TARGETS cameraunlock
    ARCHIVE DESTINATION lib
    LIBRARY DESTINATION lib
)
install(DIRECTORY include/ DESTINATION include)
