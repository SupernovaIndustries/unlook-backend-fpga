#ifndef SGM_MEM_LAYOUT_H
#define SGM_MEM_LAYOUT_H

/**
 * @file sgm_mem_layout.h
 * @brief Shared DDR3 memory layout + parameter block for the SGM-Census core.
 *
 * SINGLE SOURCE OF TRUTH used by BOTH sides:
 *   - the Vitis HLS core (gateware/hls/sgm_census.*) addresses its single
 *     `gmem` AXI-master at these byte offsets;
 *   - the host backend (host/xdma/xdma_backend.cpp) H2C/C2D-DMAs the params,
 *     left, right and disparity to/from the same offsets.
 *
 * Plain C: no ap_* / OpenCV types, so the host can include it verbatim.
 * Bump SGM_LAYOUT_VERSION on any change to this struct or the offsets.
 */

#include <stdint.h>

#define SGM_LAYOUT_VERSION 1u

/* DDR3 byte offsets (relative to the core's gmem base / the host's DMA base for
 * the MIG). The host touches only PARAMS/LEFT/RIGHT/DISP; CENSUS_*/FWDAGG are
 * core-private scratch. Slots sized for the MAX operating point below.
 *
 * DDR3 sizing: FWDAGG holds width*height*numDisparities * 2 bytes. At the
 * default 680x420 / D=128 that is ~73 MB, so the card needs >= ~256 MB DDR3.
 * Raising numDisparities raises this linearly -- see RUNBOOK. */
#define SGM_OFF_PARAMS    0x00000000u /* SgmHwParams                          */
#define SGM_OFF_LEFT      0x00100000u /* left  image,  width*height bytes      */
#define SGM_OFF_RIGHT     0x00300000u /* right image,  width*height bytes      */
#define SGM_OFF_DISP      0x00500000u /* disparity,    width*height int16      */
#define SGM_OFF_CENSUS_L  0x00800000u /* left  census, width*height uint64     */
#define SGM_OFF_CENSUS_R  0x01000000u /* right census, width*height uint64     */
#define SGM_OFF_FWDAGG    0x01800000u /* forward aggregate, W*H*D uint16       */

/* Parameter block the host writes before starting the core (mirrors the
 * relevant subset of stereo::SGMCensus::Config). All int32 for a stable ABI. */
typedef struct SgmHwParams {
    int32_t width;
    int32_t height;
    int32_t minDisparity;
    int32_t numDisparities;
    int32_t verticalSearchRange;
    int32_t censusWindowSize;     /* e.g. 7 => 7x7 = 48-bit census */
    int32_t p1;
    int32_t p2;
    int32_t uniquenessRatio;      /* 0 = uniqueness check disabled   */
    int32_t enableSubpixel;       /* 0/1: parabola subpixel refine   */
    int32_t _reserved0;
    int32_t _reserved1;
} SgmHwParams;

#endif /* SGM_MEM_LAYOUT_H */
