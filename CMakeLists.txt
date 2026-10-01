cmake_minimum_required(VERSION 3.24)
project(TW3PTHairUnlock LANGUAGES CXX)

if(NOT WIN32 OR NOT MSVC OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
  message(FATAL_ERROR "Use an x64 MSVC-compatible Windows toolchain.")
endif()
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/external/reshade/deps/imgui/imgui.h"
   OR NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/external/Detours/src/detours.h")
  message(FATAL_ERROR "Initialize external/reshade, its deps/imgui submodule, and external/Detours as described in README.md.")
endif()

set(DETOURS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/external/Detours/src")
add_library(detours STATIC
  "${DETOURS_DIR}/detours.cpp" "${DETOURS_DIR}/modules.cpp"
  "${DETOURS_DIR}/disasm.cpp" "${DETOURS_DIR}/image.cpp"
  "${DETOURS_DIR}/creatwth.cpp" "${DETOURS_DIR}/disolx86.cpp"
  "${DETOURS_DIR}/disolx64.cpp" "${DETOURS_DIR}/disolia64.cpp"
  "${DETOURS_DIR}/disolarm.cpp" "${DETOURS_DIR}/disolarm64.cpp")
target_include_directories(detours PUBLIC "${DETOURS_DIR}")
target_compile_definitions(detours PRIVATE WIN32_LEAN_AND_MEAN _WIN32_WINNT=0x601)

add_library(witcher3-pthairunlock MODULE src/addon.cpp)
target_compile_options(witcher3-pthairunlock PRIVATE /bigobj /utf-8)
target_compile_definitions(witcher3-pthairunlock PRIVATE NOMINMAX)
target_include_directories(witcher3-pthairunlock SYSTEM PRIVATE external/reshade)
target_link_libraries(witcher3-pthairunlock PRIVATE detours bcrypt dxgi ole32 psapi version)
set_target_properties(witcher3-pthairunlock PROPERTIES
  PREFIX "renodx-" SUFFIX ".addon64")

include(CTest)
if(BUILD_TESTING)
  add_executable(geometry_tests src/dots/geometry_tests.cpp src/dots/geometry.cpp)
  add_executable(identity_tests src/identity_tests.cpp)
  add_executable(lifecycle_tests src/lifecycle_tests.cpp)
  add_executable(hotfix_tests src/hotfix_tests.cpp src/dots/game_profile.cpp src/dots/shader_ir.cpp src/dots/shader_cache.cpp)
  target_link_libraries(hotfix_tests PRIVATE bcrypt ole32)
  foreach(test IN ITEMS geometry_tests identity_tests lifecycle_tests hotfix_tests)
    # These tests use assert(), including calls with required side effects.
    target_compile_options(${test} PRIVATE /UNDEBUG)
    target_compile_definitions(${test} PRIVATE NOMINMAX)
  endforeach()
  add_test(NAME geometry COMMAND geometry_tests)
  add_test(NAME native_device_identity COMMAND identity_tests)
  add_test(NAME addon_lifecycle COMMAND lifecycle_tests $<TARGET_FILE:witcher3-pthairunlock>)
  add_test(NAME hotfix_compatibility COMMAND hotfix_tests)
endif()
