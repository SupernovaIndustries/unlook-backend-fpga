#ifndef SGM_CENSUS_HLS_HPP
#define SGM_CENSUS_HLS_HPP

/**
 * @file sgm_census.hpp
 * @brief Vitis HLS SGM-Census core for the Unlook Artix-7 XC7A200T accelerator.
 *
 * Faithful hardware port of unlook-sdk/src/stereo/SGMCensus.cpp: 7x7 census
 * (48-bit), Hamming matching cost with +/- vertical search, 4-path SGM
 * (L->R, R->L, T->B, B->T) with P1/P2, WTA + uniqueness + parabola subpixel,
 * output int16 disparity scaled x16 (invalid == 0). Bit-compatible with the CPU
 * so reprojectImageTo3D(Q) downstream is unchanged.
 *
 * Single AXI-master `gmem` (offset=slave) over DDR3, ap_ctrl on s_axilite.
 * Parameters + images + scratch live in DDR3 at the offsets in sgm_mem_layout.h.
 *
 * This is the CORRECTNESS-FIRST version (two raster passes, DDR3-backed forward
 * aggregate). Throughput is tuned on hardware via HLS pragmas / tiling -- see the
 * NOTE blocks in sgm_census.cpp.
 */

#include <stdint.h>

#include "sgm_mem_layout.h"

// ---- Compile-time maxima (size on-chip buffers; actual sizes come from params).
#ifndef SGM_MAX_WIDTH
#define SGM_MAX_WIDTH 800
#endif
#ifndef SGM_MAX_HEIGHT
#define SGM_MAX_HEIGHT 600
#endif
#ifndef SGM_MAX_DISP
#define SGM_MAX_DISP 256
#endif
#ifndef SGM_MAX_VSEARCH
#define SGM_MAX_VSEARCH 4
#endif

// 7x7 census => radius 3, 48 descriptor bits (center skipped).
#define SGM_CENSUS_RADIUS 3
#define SGM_CENSUS_BITS   48

/// HLS top. `gmem` is the DDR3 base (see RegisterMap::kDdr3Base); the core reads
/// SgmHwParams/left/right and writes the disparity at the sgm_mem_layout offsets.
void sgm_census(volatile uint8_t* gmem);

#endif // SGM_CENSUS_HLS_HPP
