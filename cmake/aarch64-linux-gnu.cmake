# SPDX-License-Identifier: BSD-3-Clause
# Generic aarch64 cross toolchain (e.g. Ubuntu's gcc-aarch64-linux-gnu).
#
# Works without a BSP sysroot because the GLES backend dlopen's the Mali libs
# at runtime and the VPU/DDR backends use only kernel uAPI. The arch-neutral
# Khronos (EGL/GLES2) and linux/* uAPI headers are pulled from the host with
# lowest precedence (-idirafter), so the cross sysroot's own libc/libstdc++
# headers always win. libstdc++/libgcc are linked statically so the binary does
# not depend on the board's C++ runtime version.
#
# Use: cmake -S . -B build-aarch64 \
#        -DCMAKE_TOOLCHAIN_FILE=cmake/aarch64-linux-gnu.cmake \
#        -DIMX95_GPU=gles -DIMX95_VPU=v4l2 -DIMX95_DDR=pmu

set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)

set(CMAKE_C_COMPILER aarch64-linux-gnu-gcc)
set(CMAKE_CXX_COMPILER aarch64-linux-gnu-g++)

add_compile_options(-idirafter /usr/include)
set(CMAKE_EXE_LINKER_FLAGS_INIT "-static-libstdc++ -static-libgcc")

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY BOTH)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE BOTH)
