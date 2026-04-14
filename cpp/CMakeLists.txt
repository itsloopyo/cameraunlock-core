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
    src/memory/pattern_scanner.cpp
    src/input/hotkey_poller.cpp
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
option(CAMERAUNLOCK_BUILD_HOOKS "Build hook manager (requires MinHook)" OFF)
if(CAMERAUNLOCK_BUILD_HOOKS)
    if(NOT TARGET minhook)
        message(FATAL_ERROR "CAMERAUNLOCK_BUILD_HOOKS requires MinHook target. Please add MinHook before including cameraunlock-core.")
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

# Optional REFramework utilities module (requires REFramework headers from consumer)
option(CAMERAUNLOCK_BUILD_REFRAMEWORK "Build REFramework utilities" OFF)
if(CAMERAUNLOCK_BUILD_REFRAMEWORK)
    add_library(cameraunlock_reframework STATIC
        src/reframework/log_callback.cpp
        src/reframework/managed_utils.cpp
        src/reframework/game_state_probing.cpp
        src/reframework/tdb_inspector.cpp
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
    # REFramework headers (reframework/API.hpp) must be provided by the consumer
    # via CAMERAUNLOCK_REFRAMEWORK_INCLUDE_DIR or by adding include dirs after
    # add_subdirectory.
    if(CAMERAUNLOCK_REFRAMEWORK_INCLUDE_DIR)
        target_include_directories(cameraunlock_reframework PRIVATE
            ${CAMERAUNLOCK_REFRAMEWORK_INCLUDE_DIR}
        )
    endif()

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
