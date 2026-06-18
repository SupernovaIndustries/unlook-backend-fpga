#pragma once

/**
 * @file RegisterMap.hpp
 * @brief AXI-Lite register map of the Unlook SGM-Census FPGA core.
 *
 * SINGLE SOURCE OF TRUTH for the host<->gateware control contract. The Verilog
 * register file in gateware/rtl/ MUST mirror these offsets and the card-side
 * buffer addresses below. Bump CORE_VERSION (and the host's
 * expected_bitstream_version) on any incompatible change.
 *
 * Access: the registers live behind the XDMA `_user` BAR (AXI-Lite). Pixel/
 * disparity buffers live in card memory (BRAM or DDR3 via MIG) and are moved
 * with the XDMA H2C/C2D bulk channels at the *_ADDR offsets.
 */

#include <cstdint>

namespace unlook_fpga::reg {

// ---- AXI-Lite control/status registers (byte offsets in the _user BAR) -----
constexpr uint32_t ID          = 0x00; ///< RO: magic, must read as kMagic ("UNLK").
constexpr uint32_t VERSION     = 0x04; ///< RO: core/bitstream version.
constexpr uint32_t CTRL        = 0x08; ///< WO: bit0 START (self-clearing), bit1 RESET.
constexpr uint32_t STATUS      = 0x0C; ///< RO: bit0 DONE, bit1 BUSY, bit2 ERROR.
constexpr uint32_t IMG_W       = 0x10; ///< RW: image width  (px).
constexpr uint32_t IMG_H       = 0x14; ///< RW: image height (px).
constexpr uint32_t MIN_DISP    = 0x18; ///< RW: minimum disparity.
constexpr uint32_t NUM_DISP    = 0x1C; ///< RW: disparity search range.
constexpr uint32_t VSEARCH     = 0x20; ///< RW: vertical search range (+/- rows).
constexpr uint32_t P1          = 0x24; ///< RW: SGM small-step penalty.
constexpr uint32_t P2          = 0x28; ///< RW: SGM large-step penalty.
constexpr uint32_t UNIQUENESS  = 0x2C; ///< RW: uniqueness ratio (%), 0 = off.
constexpr uint32_t FLAGS       = 0x30; ///< RW: bit0 SUBPIXEL enable.
constexpr uint32_t LEFT_ADDR   = 0x34; ///< RW: card address of the left image buffer.
constexpr uint32_t RIGHT_ADDR  = 0x38; ///< RW: card address of the right image buffer.
constexpr uint32_t DISP_ADDR   = 0x3C; ///< RW: card address of the disparity buffer.

// ---- bit fields ------------------------------------------------------------
constexpr uint32_t CTRL_START   = 1u << 0;
constexpr uint32_t CTRL_RESET   = 1u << 1;
constexpr uint32_t STATUS_DONE  = 1u << 0;
constexpr uint32_t STATUS_BUSY  = 1u << 1;
constexpr uint32_t STATUS_ERROR = 1u << 2;
constexpr uint32_t FLAGS_SUBPIXEL = 1u << 0;

// ---- constants -------------------------------------------------------------
/// "UNLK" little-endian -> 0x4B4C4E55. Handshake magic in the ID register.
constexpr uint32_t kMagic = 0x4B4C4E55u;

/// Current core version the gateware in this repo implements. The host's
/// expected_bitstream_version (0 = skip) is checked against the VERSION reg.
constexpr uint32_t kCoreVersion = 1u;

// ---- card-side buffer layout (must match the gateware MIG/BRAM map) --------
// Default DDR3 layout: 16 MB apart, ample for 800x600 8-bit (469 KB) inputs and
// a 800x600 int16 disparity (938 KB). Tune together with the gateware.
constexpr uint64_t kLeftBufAddr  = 0x0000'0000ull;
constexpr uint64_t kRightBufAddr = 0x0100'0000ull;
constexpr uint64_t kDispBufAddr  = 0x0200'0000ull;

} // namespace unlook_fpga::reg
