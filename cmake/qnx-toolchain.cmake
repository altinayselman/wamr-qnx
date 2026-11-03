# CMake toolchain file for QNX SDP 8.0 (qcc / q++).
#
# Copyright 2026 Selman Altinay
# SPDX-License-Identifier: Apache-2.0
#
# Source the SDP environment first (qnxsdp-env.sh or qnxsdp-env.bat) so that
# QNX_HOST and QNX_TARGET are set, then configure with
#   cmake -G Ninja -DCMAKE_TOOLCHAIN_FILE=cmake/qnx-toolchain.cmake -DQNX_ARCH=x86_64 ...
# QNX_ARCH is x86_64 (default) or aarch64le.

if(NOT DEFINED ENV{QNX_TARGET} OR NOT DEFINED ENV{QNX_HOST})
  message(FATAL_ERROR "QNX_TARGET/QNX_HOST are not set: source qnxsdp-env first")
endif()

if(NOT QNX_ARCH)
  set(QNX_ARCH x86_64)
endif()
set(QNX_ARCH ${QNX_ARCH} CACHE STRING "QNX target CPU: x86_64 or aarch64le")

set(CMAKE_SYSTEM_NAME QNX)
set(CMAKE_SYSTEM_VERSION 8.0.0)
if(QNX_ARCH STREQUAL "aarch64le")
  set(CMAKE_SYSTEM_PROCESSOR aarch64)
  set(_qnx_variant gcc_ntoaarch64le)
else()
  set(CMAKE_SYSTEM_PROCESSOR x86_64)
  set(_qnx_variant gcc_ntox86_64)
endif()

file(TO_CMAKE_PATH "$ENV{QNX_HOST}" _qnx_host)
file(TO_CMAKE_PATH "$ENV{QNX_TARGET}" _qnx_target)
if(CMAKE_HOST_WIN32)
  set(_exe ".exe")
endif()

set(CMAKE_C_COMPILER "${_qnx_host}/usr/bin/qcc${_exe}")
set(CMAKE_C_COMPILER_TARGET ${_qnx_variant})
set(CMAKE_CXX_COMPILER "${_qnx_host}/usr/bin/q++${_exe}")
set(CMAKE_CXX_COMPILER_TARGET ${_qnx_variant}_cxx)
set(CMAKE_ASM_COMPILER "${_qnx_host}/usr/bin/qcc${_exe}")
set(CMAKE_ASM_COMPILER_TARGET ${_qnx_variant})

set(CMAKE_SYSROOT "${_qnx_target}")
set(CMAKE_FIND_ROOT_PATH "${_qnx_target}")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
