cmake_minimum_required(VERSION 3.20)
project(cameraunlock_isolated_input_host LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Static CRT, as the library has it: the DLL is copied alone into a game's mod
# folder and must not need a runtime the game does not ship.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

set(CAMERAUNLOCK_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(CAMERAUNLOCK_BUILD_HOOKS ON CACHE BOOL "" FORCE)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../.. ${CMAKE_BINARY_DIR}/cameraunlock)

add_library(isolated_input_host SHARED host.cpp)
set_target_properties(isolated_input_host PROPERTIES OUTPUT_NAME CameraUnlockIsolatedInput PREFIX "")
target_link_libraries(isolated_input_host PRIVATE cameraunlock minhook)
# The definitions cpp/CMakeLists.txt gives its own directory, which do not reach this one.
target_compile_definitions(isolated_input_host PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX _CRT_SECURE_NO_WARNINGS)
if(MSVC)
    target_compile_options(isolated_input_host PRIVATE /W4 /WX)
endif()
