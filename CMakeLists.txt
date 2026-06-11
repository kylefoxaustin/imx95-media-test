# SPDX-License-Identifier: BSD-3-Clause
cmake_minimum_required(VERSION 3.16)
project(imx95-media-test LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

# Host build links mock backends so the whole CLI is testable without silicon
# (including under qemu-imx95). A target build links the real GLES/V4L2/DDR-PMU
# backends behind the same factory signatures.
option(IMX95_TARGET "Build real i.MX95 backends (GLES / V4L2 / DDR PMU)" OFF)

add_executable(imx95-test
  src/main.cpp
  src/menu.cpp
  src/config.cpp
  src/term.cpp
  src/runner.cpp)

target_include_directories(imx95-test PRIVATE src)
target_compile_options(imx95-test PRIVATE -Wall -Wextra)

find_package(Threads REQUIRED)
target_link_libraries(imx95-test PRIVATE Threads::Threads)

if(IMX95_TARGET)
  # Real backends are added here as they land (src/backends_real_*.cpp).
  target_compile_definitions(imx95-test PRIVATE IMX95_TARGET=1)
  message(FATAL_ERROR "IMX95_TARGET backends not implemented yet — build the host (mock) target for now.")
else()
  target_sources(imx95-test PRIVATE src/backends_mock.cpp)
endif()
