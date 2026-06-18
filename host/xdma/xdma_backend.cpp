/**
 * @file xdma_backend.cpp
 * @brief Real Unlook FPGA stereo backend C ABI over Xilinx XDMA.
 *
 * Builds `libunlook_fpga_backend.so` driving the Artix-7 XC7A200T SGM-Census
 * core: handshake on the AXI-Lite ID/VERSION registers, H2C-upload the rectified
 * pair, trigger, poll STATUS, C2D-download the int16 (x16) disparity.
 *
 * Select at configure time: -DUNLOOK_FPGA_BACKEND=xdma.
 *
 * NOTE: the register protocol and card-buffer layout are defined in
 * RegisterMap.hpp and MUST match the gateware. Sections marked TODO(gateware)
 * are the points to confirm once the RTL register file is frozen.
 */

#include "unlook_fpga_backend.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <string>
#include <thread>

#include "RegisterMap.hpp"
#include "XdmaTransport.hpp"
#include "FpgaDetector.hpp"

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

    XdmaInstance(const UnlookSgmParams& p)
        : params(p),
          transport(p.devicePath[0] ? p.devicePath : "/dev/xdma0",
                    p.dmaTimeoutMs > 0 ? p.dmaTimeoutMs : 33) {}
};

/// Program the per-instance, frame-invariant SGM parameters + buffer addresses.
bool programStaticConfig(XdmaInstance* inst) {
    auto& t = inst->transport;
    const auto& p = inst->params;
    bool ok = true;
    ok &= t.writeReg(reg::MIN_DISP, static_cast<uint32_t>(p.minDisparity));
    ok &= t.writeReg(reg::NUM_DISP, static_cast<uint32_t>(p.numDisparities));
    ok &= t.writeReg(reg::VSEARCH, static_cast<uint32_t>(p.verticalSearchRange));
    ok &= t.writeReg(reg::P1, static_cast<uint32_t>(p.p1));
    ok &= t.writeReg(reg::P2, static_cast<uint32_t>(p.p2));
    ok &= t.writeReg(reg::UNIQUENESS, static_cast<uint32_t>(p.uniquenessRatio));
    ok &= t.writeReg(reg::FLAGS, p.enableSubpixel ? reg::FLAGS_SUBPIXEL : 0u);
    ok &= t.writeReg(reg::LEFT_ADDR, static_cast<uint32_t>(reg::kLeftBufAddr));
    ok &= t.writeReg(reg::RIGHT_ADDR, static_cast<uint32_t>(reg::kRightBufAddr));
    ok &= t.writeReg(reg::DISP_ADDR, static_cast<uint32_t>(reg::kDispBufAddr));
    return ok;
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
    auto* inst = new (std::nothrow) XdmaInstance(*params);
    if (!inst) return nullptr;

    if (!inst->transport.open()) { delete inst; return nullptr; }

    // Handshake: ID magic + (optional) version match.
    uint32_t id = 0, version = 0;
    if (!inst->transport.readReg(reg::ID, id) || id != reg::kMagic) {
        delete inst; return nullptr;
    }
    inst->transport.readReg(reg::VERSION, version);
    if (params->expectedBitstreamVersion != 0 &&
        version != params->expectedBitstreamVersion) {
        delete inst; return nullptr;
    }

    if (!programStaticConfig(inst)) { delete inst; return nullptr; }
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

    const size_t pixels    = static_cast<size_t>(width) * height;
    const size_t imgBytes  = pixels;                 // 8-bit grayscale
    const size_t dispBytes = pixels * sizeof(int16_t);

    auto bail = [&](const std::string& msg) -> int {
        if (stats) setStr(stats->errorMessage, sizeof(stats->errorMessage),
                          msg + " [" + t.lastError() + "]");
        return 1;
    };

    if (!t.writeReg(reg::IMG_W, static_cast<uint32_t>(width)))  return bail("write IMG_W");
    if (!t.writeReg(reg::IMG_H, static_cast<uint32_t>(height))) return bail("write IMG_H");

    if (!t.writeH2C(leftGray,  imgBytes, reg::kLeftBufAddr))  return bail("H2C left");
    if (!t.writeH2C(rightGray, imgBytes, reg::kRightBufAddr)) return bail("H2C right");

    if (!t.writeReg(reg::CTRL, reg::CTRL_START)) return bail("write CTRL.START");

    // Poll STATUS for DONE with a steady-clock ceiling; short sleeps avoid a
    // busy-spin contending with the camera/scan threads.
    const auto deadline = t0 + std::chrono::milliseconds(t.dmaTimeoutMs());
    uint32_t status = 0;
    for (;;) {
        if (!t.readReg(reg::STATUS, status)) return bail("read STATUS");
        if (status & reg::STATUS_ERROR) return bail("core reported STATUS.ERROR");
        if (status & reg::STATUS_DONE) break;
        if (std::chrono::steady_clock::now() >= deadline) {
            if (stats) setStr(stats->errorMessage, sizeof(stats->errorMessage),
                              "FPGA DMA/compute timeout");
            return 2;  // distinct code: lets the host audit FPGA_DMA_TIMEOUT
        }
        std::this_thread::sleep_for(std::chrono::microseconds(50));
    }

    if (!t.readC2D(disparityX16Out, dispBytes, reg::kDispBufAddr))
        return bail("C2D disparity");

    // Validity stats (invalid == 0), matching the SGMCensus convention.
    long long valid = 0;
    for (size_t i = 0; i < pixels; ++i) if (disparityX16Out[i] != 0) ++valid;

    const auto t1 = std::chrono::steady_clock::now();
    if (stats) {
        stats->totalTimeMs =
            std::chrono::duration<double, std::milli>(t1 - t0).count();
        stats->validPixels  = static_cast<int32_t>(valid);
        stats->validPercent = 100.0 * static_cast<double>(valid) /
                              static_cast<double>(pixels);
        stats->usedFpga = 1;
    }
    return 0;
}

void unlook_fpga_destroy(void* handle) {
    delete static_cast<XdmaInstance*>(handle);
}

} // extern "C"
