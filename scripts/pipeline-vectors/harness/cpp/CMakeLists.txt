cmake_minimum_required(VERSION 3.20)
project(cameraunlock_conformance_harness LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Match the library's static-CRT choice. cpp/CMakeLists.txt sets this inside its
# own directory scope, so a harness that does not repeat it links MD against an
# MT lib and fails with LNK2038.
if(WIN32)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

# The library's own tests are irrelevant here and pull in extra dependencies.
set(CAMERAUNLOCK_BUILD_TESTS OFF CACHE BOOL "" FORCE)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../../../../cpp ${CMAKE_BINARY_DIR}/cameraunlock)

add_executable(conformance_harness main.cpp)
target_link_libraries(conformance_harness PRIVATE cameraunlock)
