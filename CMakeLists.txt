# ===========================================================================
#  Unlook FPGA Backend -- external stereo accelerator plugin for the Unlook SDK
#
#  Builds libunlook_fpga_backend.so, exporting the stable C ABI in
#  include/unlook_fpga_backend.h. The Unlook SDK dlopen's it at runtime
#  (StereoBackendPlugin) to offload SGM-Census to an Artix-7 XC7A200T over
#  PCIe/XDMA: H2C the rectified pair into DDR3, run the HLS SGM-Census core,
#  C2D the int16 (x16) disparity back.
#
#  The gateware (Vitis HLS core + Vivado project) and released bitstreams live
#  under gateware/ and firmware/ and are built by their own toolchain (HLS /
#  Vivado), not by this CMake.
# ===========================================================================
cmake_minimum_required(VERSION 3.16)

project(UnlookFpgaBackend
    VERSION 0.1.0
    DESCRIPTION "Unlook FPGA stereo backend (XC7A200T over PCIe/XDMA)"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG -Wall -Wextra")
set(CMAKE_CXX_FLAGS_DEBUG   "-g -O0 -Wall -Wextra")

option(UNLOOK_FPGA_BUILD_TESTS "Build the C ABI self-test" ON)
option(UNLOOK_FPGA_BUILD_TOOLS "Build gen_golden (needs the Unlook SDK + OpenCV)" OFF)

add_subdirectory(host)

if(UNLOOK_FPGA_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

if(UNLOOK_FPGA_BUILD_TOOLS)
    add_subdirectory(tools)
endif()

message(STATUS "================= Unlook FPGA Backend ${PROJECT_VERSION} =================")
message(STATUS "  build type   : ${CMAKE_BUILD_TYPE}")
message(STATUS "  tests        : ${UNLOOK_FPGA_BUILD_TESTS}")
message(STATUS "  output       : libunlook_fpga_backend.so (XDMA)")
message(STATUS "=============================================================")
