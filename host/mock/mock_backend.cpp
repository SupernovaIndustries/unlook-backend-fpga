/**
 * @file mock_backend.cpp
 * @brief Mock implementation of the Unlook FPGA stereo backend C ABI.
 *
 * Builds `libunlook_fpga_backend.so` WITHOUT any FPGA/PCIe. It reports a device
 * as "present" and produces a deterministic synthetic disparity map (a smooth
 * left-to-right ramp scaled x16, with a thin invalid border) so the Unlook SDK
 * plugin path (StereoBackendPlugin -> PluginFpgaMatcher -> reprojectImageTo3D)
 * can be exercised end-to-end on any host, with no Artix-7 attached.
 *
 * It is the fixture that lets us pin the data contract (sizes, x16 scaling,
 * CV_16S layout, invalid==0) BEFORE the real XDMA backend + gateware exist.
 *
 * Select at configure time: -DUNLOOK_FPGA_BACKEND=mock (default).
 */

#include "unlook_fpga_backend.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <string>

namespace {

struct MockInstance {
    UnlookSgmParams params;
};

void setStr(char* dst, size_t cap, const char* src) {
    if (cap == 0) return;
    const size_t n = std::min(std::strlen(src), cap - 1);
    std::memcpy(dst, src, n);
    dst[n] = '\0';
}

} // namespace

extern "C" {

uint32_t unlook_fpga_plugin_abi_version(void) {
    return UNLOOK_FPGA_ABI_VERSION;
}

int unlook_fpga_probe(const char* devicePath, UnlookFpgaInfo* outInfo) {
    if (!outInfo) return 0;
    std::memset(outInfo, 0, sizeof(*outInfo));
    outInfo->present          = 1;
    outInfo->pciVendorId      = 0x10ee;   // Xilinx, as the real card reports.
    outInfo->pciDeviceId      = 0x7024;   // arbitrary mock id
    outInfo->bitstreamVersion = 0;        // 0 => "any" so the host handshake passes
    setStr(outInfo->devicePath, sizeof(outInfo->devicePath),
           devicePath ? devicePath : "/dev/xdma0(mock)");
    setStr(outInfo->detail, sizeof(outInfo->detail),
           "MOCK Unlook FPGA backend (synthetic disparity, no hardware)");
    return 1;
}

void* unlook_fpga_create(const UnlookSgmParams* params) {
    if (!params) return nullptr;
    auto* inst = new (std::nothrow) MockInstance();
    if (!inst) return nullptr;
    inst->params = *params;
    return inst;
}

int unlook_fpga_compute(void* handle,
                        const uint8_t* leftGray, const uint8_t* rightGray,
                        int32_t width, int32_t height,
                        int16_t* disparityX16Out,
                        UnlookFpgaStats* stats) {
    if (stats) std::memset(stats, 0, sizeof(*stats));
    auto* inst = static_cast<MockInstance*>(handle);
    if (!inst || !leftGray || !rightGray || !disparityX16Out || width <= 0 || height <= 0) {
        if (stats) setStr(stats->errorMessage, sizeof(stats->errorMessage),
                          "mock compute: invalid arguments");
        return 1;
    }
    (void)rightGray;  // mock does not actually match

    const auto t0 = std::chrono::steady_clock::now();

    const int   minD    = inst->params.minDisparity;
    const int   numD    = std::max(1, inst->params.numDisparities);
    const int   border  = std::max(1, inst->params.censusWindowSize / 2);
    const float spanDen = static_cast<float>(std::max(1, width - 1));

    long long valid = 0;
    for (int y = 0; y < height; ++y) {
        int16_t*       drow = disparityX16Out + static_cast<size_t>(y) * width;
        const uint8_t* lrow = leftGray        + static_cast<size_t>(y) * width;
        for (int x = 0; x < width; ++x) {
            // Thin invalid border (mirrors SGMCensus dropping window-radius edges)
            // and treat fully-black input pixels as invalid, so the confidence map
            // downstream is not trivially uniform.
            if (x < border || x >= width - border ||
                y < border || y >= height - border || lrow[x] == 0) {
                drow[x] = 0;
                continue;
            }
            // Smooth left-to-right ramp across the configured disparity range.
            const float dpix = static_cast<float>(minD) +
                               static_cast<float>(numD - 1) * (static_cast<float>(x) / spanDen);
            drow[x] = static_cast<int16_t>(dpix * 16.0f + 0.5f);
            ++valid;
        }
    }

    const auto t1 = std::chrono::steady_clock::now();
    if (stats) {
        stats->totalTimeMs =
            std::chrono::duration<double, std::milli>(t1 - t0).count();
        stats->validPixels  = static_cast<int32_t>(valid);
        stats->validPercent =
            100.0 * static_cast<double>(valid) / (static_cast<double>(width) * height);
        stats->usedFpga = 0;  // synthetic, not real silicon
    }
    return 0;
}

void unlook_fpga_destroy(void* handle) {
    delete static_cast<MockInstance*>(handle);
}

} // extern "C"
