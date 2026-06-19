#pragma once

/**
 * @file RegisterMap.hpp
 * @brief AXI-Lite control map of the Vitis HLS sgm_census core + DDR3 layout.
 *
 * The HLS core `sgm_census(volatile uint8_t* gmem)` exposes the standard HLS
 * ap_ctrl_hs control protocol on its s_axilite bundle (reached through the XDMA
 * `_user` BAR), plus the 64-bit base-address register of its single `gmem`
 * AXI-master argument. These offsets are FIXED by the HLS control protocol for
 * a single pointer argument, so the host needs no per-build generated header.
 *
 * All pixel/params/disparity traffic goes through DDR3 at the offsets in
 * <sgm_mem_layout.h> (shared with the core), moved by the XDMA H2C/C2H channels.
 */

#include <cstdint>

#include "sgm_mem_layout.h"

namespace unlook_fpga::reg {

// ---- HLS ap_ctrl_hs control registers (s_axilite "control" bundle) ---------
constexpr uint32_t AP_CTRL      = 0x00; ///< bit0 ap_start, bit1 ap_done, bit2 ap_idle, bit3 ap_ready.
constexpr uint32_t GIE          = 0x04; ///< Global interrupt enable (unused; polling).
constexpr uint32_t IER          = 0x08; ///< IP interrupt enable.
constexpr uint32_t ISR          = 0x0C; ///< IP interrupt status.
// Single `gmem` pointer arg (offset=slave) -> 64-bit base-address register.
constexpr uint32_t GMEM_ADDR_LO = 0x10;
constexpr uint32_t GMEM_ADDR_HI = 0x14;

// ---- ap_ctrl bit fields ----------------------------------------------------
constexpr uint32_t AP_START = 1u << 0;
constexpr uint32_t AP_DONE  = 1u << 1;
constexpr uint32_t AP_IDLE  = 1u << 2;
constexpr uint32_t AP_READY = 1u << 3;

// ---- DDR3 base address as seen by the core's gmem master -------------------
// Assigned in the Vivado address editor (gmem -> MIG). The host's H2C/C2H DMA
// reaches the same DDR3 at the SGM_OFF_* offsets from sgm_mem_layout.h.
constexpr uint64_t kDdr3Base = 0x8000'0000ull;  // DDR3 (MIG) base on XDMA M_AXI

// PCI vendor id the XDMA endpoint must report (Xilinx) for presence detection.
constexpr uint16_t kXilinxVendorId = 0x10ee;

} // namespace unlook_fpga::reg
