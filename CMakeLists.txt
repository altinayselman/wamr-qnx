# Native QNX parts of wamr-qnx: the IPC native library loaded by iwasm and the
# native example programs. iwasm itself is built from the patched upstream tree
# (see scripts/build-qnx.sh).
#
# Copyright 2026 Selman Altinay
# SPDX-License-Identifier: Apache-2.0
cmake_minimum_required(VERSION 3.21)
project(wamr_qnx C)

if(NOT CMAKE_SYSTEM_NAME STREQUAL "QNX")
  message(FATAL_ERROR "configure with -DCMAKE_TOOLCHAIN_FILE=cmake/qnx-toolchain.cmake")
endif()
if(NOT WAMR_ROOT)
  set(WAMR_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/upstream/wasm-micro-runtime)
endif()

set(CMAKE_C_STANDARD 11)
add_compile_options(-Wall -Wextra -O2)

# iwasm --native-lib=libwamr_qnx_ipc.so
add_library(wamr_qnx_ipc SHARED qnx-ipc/wamr_qnx_ipc.c)
target_include_directories(wamr_qnx_ipc PRIVATE
  qnx-ipc/include ${WAMR_ROOT}/core/iwasm/include)
# The symbols it calls (wasm_runtime_*) are resolved from iwasm at load time.
target_link_options(wamr_qnx_ipc PRIVATE -Wl,--allow-shlib-undefined)

add_executable(sensor_sim examples/sensor-filter/sensor_sim.c)
target_include_directories(sensor_sim PRIVATE qnx-ipc/include examples/sensor-filter)
target_link_libraries(sensor_sim m)

install(TARGETS wamr_qnx_ipc LIBRARY DESTINATION lib)
install(TARGETS sensor_sim RUNTIME DESTINATION bin)
