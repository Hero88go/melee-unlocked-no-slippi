cmake_minimum_required(VERSION 3.15)
project(MeleeSourcePort LANGUAGES CXX)
enable_testing()
find_package(Python3 COMPONENTS Interpreter REQUIRED)
if(NOT WIN32 OR NOT MSVC OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
  message(FATAL_ERROR "The host requires Windows x64 and MSVC")
endif()

# The instruction set baseline for every target, matching the game library: AVX2, AVX or SSE2.
set(MELEE_CPU_BASELINE "AVX2" CACHE STRING "Instruction set baseline: AVX2, AVX or SSE2")
set_property(CACHE MELEE_CPU_BASELINE PROPERTY STRINGS AVX2 AVX SSE2)
if(MELEE_CPU_BASELINE STREQUAL "SSE2")
  set(MELEE_ARCH_FLAG "")
else()
  set(MELEE_ARCH_FLAG "/arch:${MELEE_CPU_BASELINE}")
endif()

# The host's animation adapter, generated from three files of the public decompilation.
set(HOST_FOBJ "${CMAKE_CURRENT_BINARY_DIR}/generated/FObjHost.cpp")
add_custom_command(OUTPUT "${HOST_FOBJ}"
  COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/generate_fobj_host.py" "${HOST_FOBJ}"
  DEPENDS tools/generate_fobj_host.py melee/src/sysdolphin/baselib/fobj.c
          melee/src/sysdolphin/baselib/fobj.h melee/src/sysdolphin/baselib/spline.c)
add_library(native_animation STATIC "${HOST_FOBJ}")
target_include_directories(native_animation PUBLIC native)
target_compile_features(native_animation PUBLIC cxx_std_14)
target_compile_options(native_animation PRIVATE ${MELEE_ARCH_FLAG})

# This tree holds only the Source Port without the Slippi layer.
set(MELEE_NO_SLIPPI ON CACHE BOOL "Build only melee_source, without the Slippi layer" FORCE)
add_subdirectory(port)
