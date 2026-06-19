/**
 * @file xdma_backend.cpp
 * @brief Unlook FPGA stereo backend C ABI over Xilinx XDMA, driving the Vitis
 *        HLS sgm_census core on an Artix-7 XC7A200T.
 *
 * Flow per compute():
 *   1. write the SgmHwParams block + the rectified L/R images into DDR3 (H2C),
 *      at the fixed offsets in sgm_mem_layout.h;
 *   2. point the core's gmem master at the DDR3 base (AXI-Lite) and set ap_start;
 *   3. poll ap_done with a steady-clock timeout;
 *   4. read the int16 (x16) disparity back from DDR3 (C2H).
 *
 * Disparity is int16 scaled x16 (invalid == 0) -- the exact SGMCensus layout, so
 * the SDK's Q matrix / reprojectImageTo3D is unchanged. Never throws.
 */

#include "unlook_fpga_backend.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <new>
#include <string>
#include <thread>
#include <vector>

#include "FpgaDetector.hpp"
#include "RegisterMap.hpp"
#include "XdmaTransport.hpp"
#include "sgm_mem_layout.h"

namespace {

using namespace unlook_fpga;

void setStr(char* dst, size_t cap, const std::string& src) {
    if (cap == 0) return;
    const size_t n = std::min(src.size(), cap - 1);
    std::memcpy(dst, src.data(), n);
    dst[n] = '\0';
}

struct XdmaInstance {
    UnlookSgmParams params;
    XdmaTransport   transport;

    explicit XdmaInstance(const UnlookSgmParams& p)
        : params(p),
          transport(p.devicePath[0] ? p.devicePath : "/dev/xdma0",
                    p.dmaTimeoutMs > 0 ? p.dmaTimeoutMs : 33) {}
};

SgmHwParams toHwParams(const UnlookSgmParams& p, int32_t width, int32_t height) {
    SgmHwParams hw;
    std::memset(&hw, 0, sizeof(hw));
    hw.width               = width;
    hw.height              = height;
    hw.minDisparity        = p.minDisparity;
    hw.numDisparities      = p.numDisparities;
    hw.verticalSearchRange = p.verticalSearchRange;
    hw.censusWindowSize    = p.censusWindowSize;
    hw.p1                  = p.p1;
    hw.p2                  = p.p2;
    hw.uniquenessRatio     = p.uniquenessRatio;
    hw.enableSubpixel      = p.enableSubpixel;
    return hw;
}

} // namespace

extern "C" {

uint32_t unlook_fpga_plugin_abi_version(void) {
    return UNLOOK_FPGA_ABI_VERSION;
}

int unlook_fpga_probe(const char* devicePath, UnlookFpgaInfo* outInfo) {
    if (!outInfo) return 0;
    std::memset(outInfo, 0, sizeof(*outInfo));
    const ProbeResult pr = probeFpga(devicePath ? devicePath : "/dev/xdma0");
    outInfo->present          = pr.present ? 1 : 0;
    outInfo->pciVendorId      = pr.pciVendorId;
    outInfo->pciDeviceId      = pr.pciDeviceId;
    outInfo->bitstreamVersion = pr.bitstreamVersion;
    setStr(outInfo->devicePath, sizeof(outInfo->devicePath), pr.devicePath);
    setStr(outInfo->detail, sizeof(outInfo->detail), pr.detail);
    return pr.present ? 1 : 0;
}

void* unlook_fpga_create(const UnlookSgmParams* params) {
    if (!params) return nullptr;

    const std::string dev = params->devicePath[0] ? params->devicePath : "/dev/xdma0";
    if (!probeFpga(dev).present) return nullptr;

    auto* inst = new (std::nothrow) XdmaInstance(*params);
    if (!inst) return nullptr;
    if (!inst->transport.open()) { delete inst; return nullptr; }

    // Point the core's gmem AXI-master at the DDR3 base (64-bit address reg).
    if (!inst->transport.writeReg(reg::GMEM_ADDR_LO,
                                  static_cast<uint32_t>(reg::kDdr3Base & 0xFFFFFFFFu)) ||
        !inst->transport.writeReg(reg::GMEM_ADDR_HI,
                                  static_cast<uint32_t>(reg::kDdr3Base >> 32))) {
        delete inst;
        return nullptr;
    }
    return inst;
}

int unlook_fpga_compute(void* handle,
                        const uint8_t* leftGray, const uint8_t* rightGray,
                        int32_t width, int32_t height,
                        int16_t* disparityX16Out,
                        UnlookFpgaStats* stats) {
    if (stats) std::memset(stats, 0, sizeof(*stats));
    auto* inst = static_cast<XdmaInstance*>(handle);
    if (!inst || !leftGray || !rightGray || !disparityX16Out ||
        width <= 0 || height <= 0) {
        if (stats) setStr(stats->errorMessage, sizeof(stats->errorMessage),
                          "xdma compute: invalid arguments");
        return 1;
    }

    auto& t = inst->transport;
    const auto t0 = std::chrono::steady_clock::now();

    const size_t imgBytes  = static_cast<size_t>(width) * height;
    const size_t dispBytes = imgBytes * sizeof(int16_t);

    auto bail = [&](const std::string& msg) -> int {
        if (stats) setStr(stats->errorMessage, sizeof(stats->errorMessage),
                          msg + " [" + t.lastError() + "]");
        return 1;
    };

    // 1. params + images -> DDR3
    const SgmHwParams hw = toHwParams(inst->params, width, height);
    if (!t.writeH2C(&hw, sizeof(hw), reg::kDdr3Base + SGM_OFF_PARAMS))
        return bail("H2C params");
    if (!t.writeH2C(leftGray, imgBytes, reg::kDdr3Base + SGM_OFF_LEFT))
        return bail("H2C left");
    if (!t.writeH2C(rightGray, imgBytes, reg::kDdr3Base + SGM_OFF_RIGHT))
        return bail("H2C right");

    // 2. start the HLS core (ap_start) -- ap_ctrl_hs.
    if (!t.writeReg(reg::AP_CTRL, reg::AP_START)) return bail("write ap_start");

    // 3. poll ap_done with a steady-clock ceiling (short sleeps, no busy-spin).
    const auto deadline = t0 + std::chrono::milliseconds(t.dmaTimeoutMs());
    uint32_t ctrl = 0;
    for (;;) {
        if (!t.readReg(reg::AP_CTRL, ctrl)) return bail("read ap_ctrl");
        if (ctrl & reg::AP_DONE) break;
        if (std::chrono::steady_clock::now() >= deadline) {
            if (stats) setStr(stats->errorMessage, sizeof(stats->errorMessage),
                              "FPGA compute timeout (ap_done not asserted)");
            return 2;  // distinct code -> host audits FPGA_DMA_TIMEOUT
        }
        std::this_thread::sleep_for(std::chrono::microseconds(50));
    }

    // 4. disparity <- DDR3
    if (!t.readC2H(disparityX16Out, dispBytes, reg::kDdr3Base + SGM_OFF_DISP))
        return bail("C2H disparity");

    long long valid = 0;
    for (size_t i = 0; i < imgBytes; ++i) if (disparityX16Out[i] != 0) ++valid;

    const auto t1 = std::chrono::steady_clock::now();
    if (stats) {
        stats->totalTimeMs =
            std::chrono::duration<double, std::milli>(t1 - t0).count();
        stats->validPixels  = static_cast<int32_t>(valid);
        stats->validPercent = 100.0 * static_cast<double>(valid) /
                              static_cast<double>(imgBytes);
        stats->usedFpga = 1;
    }
    return 0;
}

void unlook_fpga_destroy(void* handle) {
    delete static_cast<XdmaInstance*>(handle);
}

} // extern "C"
