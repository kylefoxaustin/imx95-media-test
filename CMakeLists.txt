# SPDX-License-Identifier: BSD-3-Clause
cmake_minimum_required(VERSION 3.16)
project(imx95-media-test LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

# Each subsystem's backend is selected independently so real backends can land
# one at a time and be mixed with mocks. Mock backends run anywhere (host, CI,
# qemu-imx95). The GLES GPU backend builds and runs on host Mesa *and* on the
# i.MX95 Mali driver from the same source.
set(IMX95_GPU "mock" CACHE STRING "GPU backend: mock | gles")
set(IMX95_VPU "mock" CACHE STRING "VPU backend: mock | v4l2 (todo)")
set(IMX95_DDR "mock" CACHE STRING "DDR monitor: mock | pmu (todo)")

add_executable(imx95-test
  src/main.cpp
  src/menu.cpp
  src/config.cpp
  src/term.cpp
  src/runner.cpp
  src/backends/traffic_estimate.cpp)

target_include_directories(imx95-test PRIVATE src)
target_compile_options(imx95-test PRIVATE -Wall -Wextra)

find_package(Threads REQUIRED)
target_link_libraries(imx95-test PRIVATE Threads::Threads)

# --- GPU ---------------------------------------------------------------------
if(IMX95_GPU STREQUAL "gles")
  find_package(PkgConfig REQUIRED)
  pkg_check_modules(EGL REQUIRED egl)
  pkg_check_modules(GLES2 REQUIRED glesv2)
  target_sources(imx95-test PRIVATE src/backends/gpu_gles.cpp)
  target_include_directories(imx95-test PRIVATE ${EGL_INCLUDE_DIRS} ${GLES2_INCLUDE_DIRS})
  target_link_libraries(imx95-test PRIVATE ${EGL_LIBRARIES} ${GLES2_LIBRARIES})
elseif(IMX95_GPU STREQUAL "mock")
  target_sources(imx95-test PRIVATE src/backends/gpu_mock.cpp)
else()
  message(FATAL_ERROR "IMX95_GPU must be 'mock' or 'gles' (got '${IMX95_GPU}')")
endif()

# --- VPU ---------------------------------------------------------------------
if(IMX95_VPU STREQUAL "v4l2")
  # V4L2 mem2mem codec: just kernel ioctls, no extra libraries to link.
  target_sources(imx95-test PRIVATE src/backends/vpu_v4l2.cpp src/backends/v4l2_m2m.cpp)
elseif(IMX95_VPU STREQUAL "mock")
  target_sources(imx95-test PRIVATE src/backends/vpu_mock.cpp)
else()
  message(FATAL_ERROR "IMX95_VPU must be 'mock' or 'v4l2' (got '${IMX95_VPU}')")
endif()

# --- DDR ---------------------------------------------------------------------
if(IMX95_DDR STREQUAL "mock")
  target_sources(imx95-test PRIVATE src/backends/ddr_mock.cpp)
else()
  message(FATAL_ERROR "IMX95_DDR backend '${IMX95_DDR}' not implemented yet (use 'mock')")
endif()

message(STATUS "Backends:  gpu=${IMX95_GPU}  vpu=${IMX95_VPU}  ddr=${IMX95_DDR}")
