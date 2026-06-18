# libunlook_fpga_backend.so -- the C ABI plugin the Unlook SDK dlopen's.
# Real Xilinx XDMA backend: AXI-Lite control (HLS ap_ctrl) + H2C/C2D DMA to DDR3.

add_library(unlook_fpga_backend SHARED
    xdma/xdma_backend.cpp
    xdma/XdmaTransport.cpp
    xdma/FpgaDetector.cpp)

set_target_properties(unlook_fpga_backend PROPERTIES
    CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF
    POSITION_INDEPENDENT_CODE ON
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR})

target_include_directories(unlook_fpga_backend
    PUBLIC  ${PROJECT_SOURCE_DIR}/include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/xdma
            ${PROJECT_SOURCE_DIR}/gateware/hls)   # shared sgm_mem_layout.h

# Hide everything except the documented C ABI symbols.
target_link_options(unlook_fpga_backend PRIVATE
    "-Wl,--version-script=${CMAKE_CURRENT_SOURCE_DIR}/unlook_fpga_backend.map")

include(GNUInstallDirs)
install(TARGETS unlook_fpga_backend LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(FILES ${PROJECT_SOURCE_DIR}/include/unlook_fpga_backend.h
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
