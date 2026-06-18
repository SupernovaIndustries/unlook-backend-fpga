# gen_golden -- reference-vector generator. Needs the Unlook SDK (SGMCensus) +
# OpenCV, so it is OFF by default. Enable with -DUNLOOK_FPGA_BUILD_TOOLS=ON and
# point CMake at an installed/built SDK:  -DUnlookSDK_DIR=<sdk>/lib/cmake/UnlookSDK
find_package(OpenCV 4.0 REQUIRED)
find_package(UnlookSDK REQUIRED)

add_executable(gen_golden gen_golden.cpp)
target_include_directories(gen_golden PRIVATE ${PROJECT_SOURCE_DIR}/gateware/hls)
target_link_libraries(gen_golden PRIVATE UnlookSDK::unlook_sdk ${OpenCV_LIBS})
