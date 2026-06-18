#ifndef UNLOOK_FPGA_BACKEND_H
#define UNLOOK_FPGA_BACKEND_H

/**
 * @file unlook_fpga_backend.h
 * @brief Stable C ABI between the Unlook SDK (host) and the external FPGA
 *        stereo backend (`libunlook_fpga_backend.so`, repo `unlook-backend-fpga`).
 *
 * The SDK does NOT contain any XDMA / PCIe / gateware code: it `dlopen`s a shared
 * library that exports the functions below and drives the Artix-7 XC7A200T
 * SGM-Census core. A plain C ABI (not C++) is used on purpose so the boundary is
 * stable and versionable across compilers/toolchains -- the professional choice
 * for a defense-grade, independently-built component.
 *
 * Disparity contract (must match stereo::SGMCensus exactly so the existing Q
 * matrix / reprojectImageTo3D path downstream is unchanged):
 *   - inputs : two rectified 8-bit grayscale images, same size, row-major.
 *   - output : int16 disparity scaled x16 (CV_16SC1 layout), invalid pixel == 0.
 * The host converts x16 -> CV_32F pixel disparities via (1/16.0).
 *
 * This header is the single source of truth for the contract and is shared
 * VERBATIM with the Unlook SDK (include/unlook/fpga/unlook_fpga_backend.h).
 * Keep the two copies byte-identical; bump UNLOOK_FPGA_ABI_VERSION on any change.
 */

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/// ABI revision. The host refuses to use a plugin whose
/// unlook_fpga_plugin_abi_version() does not match this value.
#define UNLOOK_FPGA_ABI_VERSION 1u

/// SGM parameters + device selection passed when creating a matcher instance.
/// Mirrors the relevant subset of stereo::SGMCensus::Config.
typedef struct UnlookSgmParams {
    int32_t  imageWidth;            ///< Expected input width (px).
    int32_t  imageHeight;           ///< Expected input height (px).
    int32_t  minDisparity;          ///< Minimum disparity.
    int32_t  numDisparities;        ///< Disparity search range.
    int32_t  verticalSearchRange;   ///< +/- rows searched (epipolar slack).
    int32_t  censusWindowSize;      ///< Census window (e.g. 7 => 7x7).
    int32_t  p1;                    ///< SGM small-step penalty.
    int32_t  p2;                    ///< SGM large-step penalty.
    int32_t  uniquenessRatio;       ///< Uniqueness margin (%), 0 = disabled.
    int32_t  enableSubpixel;        ///< Bool (0/1): parabola subpixel refinement.
    char     devicePath[256];       ///< e.g. "/dev/xdma0".
    uint32_t expectedBitstreamVersion; ///< 0 = skip the version handshake.
    int32_t  dmaTimeoutMs;          ///< Per-transfer ceiling (ms).
} UnlookSgmParams;

/// Filled by unlook_fpga_probe().
typedef struct UnlookFpgaInfo {
    int32_t  present;          ///< 1 if a usable Unlook FPGA core was found.
    uint16_t pciVendorId;      ///< Expect 0x10ee (Xilinx).
    uint16_t pciDeviceId;
    uint32_t bitstreamVersion; ///< Version register read from the core (0 if N/A).
    char     devicePath[256];  ///< Resolved device that responded.
    char     detail[256];      ///< Human/audit string.
} UnlookFpgaInfo;

/// Filled by unlook_fpga_compute().
typedef struct UnlookFpgaStats {
    double  totalTimeMs;       ///< Wall time of the compute (upload+run+readback).
    int32_t validPixels;       ///< Count of valid (non-zero) disparities.
    double  validPercent;      ///< Percentage of valid disparities.
    int32_t usedFpga;          ///< 1 = ran on FPGA, 0 = plugin's internal CPU fallback.
    char    errorMessage[256]; ///< Set when the return code is non-zero.
} UnlookFpgaStats;

/// Return the ABI revision the plugin was built against (see UNLOOK_FPGA_ABI_VERSION).
uint32_t unlook_fpga_plugin_abi_version(void);

/// Cheap, stateless probe for a present Unlook FPGA core. No handle is retained.
/// @param devicePath device to probe (may be NULL => plugin default).
/// @param outInfo    filled on return (must be non-NULL).
/// @return 1 if present, 0 otherwise.
int unlook_fpga_probe(const char* devicePath, UnlookFpgaInfo* outInfo);

/// Create a matcher instance bound to the device in @p params (opens XDMA,
/// performs the ID/VERSION handshake). @return opaque handle, or NULL on failure.
void* unlook_fpga_create(const UnlookSgmParams* params);

/// Compute a disparity map. Never throws / never aborts; report failure via the
/// return code + stats->errorMessage.
/// @param handle            instance from unlook_fpga_create().
/// @param leftGray,rightGray contiguous 8-bit grayscale, width*height bytes each.
/// @param width,height       image dimensions (must match the created params).
/// @param disparityX16Out    caller-allocated int16 buffer, width*height elements,
///                           filled with disparity*16 (invalid == 0).
/// @param stats              filled with timing/validity (must be non-NULL).
/// @return 0 on success, non-zero on failure.
int unlook_fpga_compute(void* handle,
                        const uint8_t* leftGray, const uint8_t* rightGray,
                        int32_t width, int32_t height,
                        int16_t* disparityX16Out,
                        UnlookFpgaStats* stats);

/// Destroy an instance and release the device. Safe to call with NULL.
void unlook_fpga_destroy(void* handle);

#ifdef __cplusplus
} // extern "C"
#endif

#endif // UNLOOK_FPGA_BACKEND_H
