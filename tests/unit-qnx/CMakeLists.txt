# Builds the upstream WAMR unit tests (tests/unit) that need neither LLVM nor
# a host wasi-sdk, cross-compiled for QNX 8.0. The binaries are run on the
# QNX target by scripts/target/unit-tests.sh.
#
# Copyright 2026 Selman Altinay
# SPDX-License-Identifier: Apache-2.0
cmake_minimum_required(VERSION 3.14)
project(wamr-unit-qnx)

if(NOT WAMR_ROOT)
  get_filename_component(WAMR_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../../upstream/wasm-micro-runtime ABSOLUTE)
endif()
set(UNIT ${WAMR_ROOT}/tests/unit)

set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
set(CMAKE_BUILD_TYPE Debug)
set(WAMR_BUILD_PLATFORM qnx)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)

include(FetchContent)
FetchContent_Declare(googletest
  URL https://github.com/google/googletest/archive/03597a01ee50ed33e9dfd640b249b4be3799d395.zip
  DOWNLOAD_EXTRACT_TIMESTAMP ON)
FetchContent_MakeAvailable(googletest)
set(GOOGLETEST_INCLUDED 1)
include(GoogleTest)
enable_testing()

# Upstream suites that build without LLVM (the AOT compiler) or wasi-sdk.
foreach(suite IN ITEMS wasm-vm interpreter wasm-c-api libc-builtin shared-utils
                       linear-memory-wasm linear-memory-aot aot-stack-frame gc
                       tid-allocator)
  add_subdirectory(${UNIT}/${suite} ${CMAKE_BINARY_DIR}/${suite})
endforeach()
