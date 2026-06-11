# SPDX-License-Identifier: BSD-3-Clause
cmake_minimum_required(VERSION 3.16)
project(imx95-media-test VERSION 1.0.2 LANGUAGES CXX)

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
target_compile_definitions(imx95-test PRIVATE IMX95_VERSION="${PROJECT_VERSION}")

find_package(Threads REQUIRED)
target_link_libraries(imx95-test PRIVATE Threads::Threads)

# --- GPU ---------------------------------------------------------------------
if(IMX95_GPU STREQUAL "gles")
  # EGL/GLES are dlopen'd at runtime (see gpu_gles.cpp), so we link only libdl
  # and need just the Khronos headers at build time — no Mali libs, which lets
  # this cross-compile without a BSP sysroot.
  target_sources(imx95-test PRIVATE src/backends/gpu_gles.cpp)
  target_link_libraries(imx95-test PRIVATE ${CMAKE_DL_LIBS})
elseif(IMX95_GPU STREQUAL "mock")
  target_sources(imx95-test PRIVATE src/backends/gpu_mock.cpp)
else()
  message(FATAL_ERROR "IMX95_GPU must be 'mock' or 'gles' (got '${IMX95_GPU}')")
endif()

# --- VPU ---------------------------------------------------------------------
if(IMX95_VPU STREQUAL "v4l2")
  # V4L2 mem2mem codec: just kernel ioctls, no extra libraries to link.
  target_sources(imx95-test PRIVATE src/backends/vpu_v4l2.cpp src/backends/v4l2_m2m.cpp
                                    src/backends/embedded_clips.cpp)
  # Bake the real Big Buck Bunny H.264 clips into the binary when present, so
  # decode has representative content with no file to upload (fetch-assets.sh
  # generates them; they are gitignored). Falls back to a synthetic bitstream.
  set(IMX95_CLIPS_DIR "${CMAKE_SOURCE_DIR}/assets/clips")
  if(EXISTS "${IMX95_CLIPS_DIR}/bbb_720p.h264" AND
     EXISTS "${IMX95_CLIPS_DIR}/bbb_1080p.h264" AND
     EXISTS "${IMX95_CLIPS_DIR}/bbb_4k.h264")
    enable_language(ASM)
    configure_file(src/backends/clips.S.in "${CMAKE_BINARY_DIR}/clips.S" @ONLY)
    target_sources(imx95-test PRIVATE "${CMAKE_BINARY_DIR}/clips.S")
    target_compile_definitions(imx95-test PRIVATE IMX95_HAVE_CLIPS=1)
    message(STATUS "Embedding Big Buck Bunny clips from ${IMX95_CLIPS_DIR}")
  else()
    message(STATUS "No embedded clips (assets/clips/*.h264 absent); decode uses synthetic bitstream")
  endif()
elseif(IMX95_VPU STREQUAL "mock")
  target_sources(imx95-test PRIVATE src/backends/vpu_mock.cpp)
else()
  message(FATAL_ERROR "IMX95_VPU must be 'mock' or 'v4l2' (got '${IMX95_VPU}')")
endif()

# --- DDR ---------------------------------------------------------------------
if(IMX95_DDR STREQUAL "pmu")
  # i.MX9 DDR PMU via perf_event_open: kernel uAPI only, no libraries.
  target_sources(imx95-test PRIVATE src/backends/ddr_pmu.cpp)
elseif(IMX95_DDR STREQUAL "mock")
  target_sources(imx95-test PRIVATE src/backends/ddr_mock.cpp)
else()
  message(FATAL_ERROR "IMX95_DDR must be 'mock' or 'pmu' (got '${IMX95_DDR}')")
endif()

message(STATUS "Backends:  gpu=${IMX95_GPU}  vpu=${IMX95_VPU}  ddr=${IMX95_DDR}")
