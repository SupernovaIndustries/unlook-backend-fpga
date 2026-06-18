# libunlook_fpga_backend.so -- the C ABI plugin the Unlook SDK dlopen's.
# Sources are chosen by UNLOOK_FPGA_BACKEND (mock | xdma); both export the
# identical symbol set from <unlook_fpga_backend.h>.

if(UNLOOK_FPGA_BACKEND STREQUAL "xdma")
    set(BACKEND_SOURCES
        xdma/xdma_backend.cpp
        xdma/XdmaTransport.cpp
        xdma/FpgaDetector.cpp)
elseif(UNLOOK_FPGA_BACKEND STREQUAL "mock")
    set(BACKEND_SOURCES
        mock/mock_backend.cpp)
else()
    message(FATAL_ERROR
        "UNLOOK_FPGA_BACKEND must be 'mock' or 'xdma' (got '${UNLOOK_FPGA_BACKEND}')")
endif()

add_library(unlook_fpga_backend SHARED ${BACKEND_SOURCES})

set_target_properties(unlook_fpga_backend PROPERTIES
    CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF
    POSITION_INDEPENDENT_CODE ON
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR})

target_include_directories(unlook_fpga_backend
    PUBLIC  ${PROJECT_SOURCE_DIR}/include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/xdma)

# Hide everything except the documented C ABI symbols.
target_link_options(unlook_fpga_backend PRIVATE
    "-Wl,--version-script=${CMAKE_CURRENT_SOURCE_DIR}/unlook_fpga_backend.map")

include(GNUInstallDirs)
install(TARGETS unlook_fpga_backend LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(FILES ${PROJECT_SOURCE_DIR}/include/unlook_fpga_backend.h
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
