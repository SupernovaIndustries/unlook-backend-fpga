# ===========================================================================
#  Unlook FPGA Backend -- external stereo accelerator plugin for the Unlook SDK
#
#  Builds libunlook_fpga_backend.so, exporting the stable C ABI in
#  include/unlook_fpga_backend.h. The Unlook SDK dlopen's it at runtime
#  (StereoBackendPlugin) to offload SGM-Census to an Artix-7 XC7A200T.
#
#  Two interchangeable implementations select the SAME library name:
#    -DUNLOOK_FPGA_BACKEND=mock   synthetic disparity, no hardware (default)
#    -DUNLOOK_FPGA_BACKEND=xdma   real Xilinx XDMA path (needs the FPGA + driver)
#
#  The gateware (HDL/Vivado) and released bitstreams live under gateware/ and
#  firmware/ and are built by their own toolchain, not by this CMake.
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

set(UNLOOK_FPGA_BACKEND "mock" CACHE STRING
    "Backend implementation: mock (synthetic, default) or xdma (real hardware)")
set_property(CACHE UNLOOK_FPGA_BACKEND PROPERTY STRINGS mock xdma)

option(UNLOOK_FPGA_BUILD_TESTS "Build the C ABI self-test" ON)

add_subdirectory(host)

if(UNLOOK_FPGA_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

message(STATUS "================= Unlook FPGA Backend ${PROJECT_VERSION} =================")
message(STATUS "  backend impl : ${UNLOOK_FPGA_BACKEND}")
message(STATUS "  build type   : ${CMAKE_BUILD_TYPE}")
message(STATUS "  tests        : ${UNLOOK_FPGA_BUILD_TESTS}")
message(STATUS "  output       : libunlook_fpga_backend.so")
message(STATUS "=============================================================")
