/**
 * @file sgm_census.cpp
 * @brief Vitis HLS SGM-Census core -- faithful port of SGMCensus.cpp.
 *
 * Two raster passes over the image (forward: L->R + T->B, backward: R->L + B->T)
 * reproduce the CPU's 4-path SGM exactly; the forward aggregate is staged in
 * DDR3, the backward pass sums it and runs WTA+subpixel inline. Census images
 * are precomputed to DDR3 and re-read (windowed) by both passes.
 *
 * Output: int16 disparity * 16 (invalid == 0), identical to SGMCensus -> the
 * SDK's reprojectImageTo3D(Q) is unchanged.
 *
 * NOTE (throughput): this is the correctness-first version. The DDR3-staged
 * forward aggregate and per-row census re-reads are the obvious optimization
 * targets (dataflow/tiling/on-chip cost reuse) once parity is confirmed in
 * cosim -- iterate on hardware. Functional parity is the priority here.
 */

#include "sgm_census.hpp"

#include <cstring>

namespace {

constexpr int      kCostMax = 255;
constexpr uint16_t kU16Max  = 0xFFFF;

inline int popcount64(uint64_t v) {
    int c = 0;
POPC: for (int i = 0; i < 64; ++i) {
        c += (int)(v & 1ull);
        v >>= 1;
    }
    return c;
}

inline uint16_t u16min(uint16_t a, uint16_t b) { return a < b ? a : b; }

// One SGM path step for all D disparities (mirrors processSGMPath()).
void sgmStep(bool boundary, const uint8_t cost[SGM_MAX_DISP],
             const uint16_t prev[SGM_MAX_DISP], uint16_t out[SGM_MAX_DISP],
             int D, int P1, int P2) {
    if (boundary) {
    BCP: for (int d = 0; d < D; ++d) out[d] = cost[d];
        return;
    }
    uint16_t minPrev = kU16Max;
MINP: for (int d = 0; d < D; ++d) minPrev = u16min(minPrev, prev[d]);
STEP: for (int d = 0; d < D; ++d) {
        uint16_t same  = prev[d];
        uint16_t minus = (d > 0)     ? (uint16_t)(prev[d - 1] + P1) : kU16Max;
        uint16_t plus  = (d < D - 1) ? (uint16_t)(prev[d + 1] + P1) : kU16Max;
        uint16_t nb    = u16min(minus, plus);
        uint16_t other = (uint16_t)(minPrev + P2);
        uint16_t mp    = u16min(same, u16min(nb, other));
        out[d] = (uint16_t)(cost[d] + mp - minPrev);  // mp >= minPrev => no underflow
    }
}

// ---- Census transform of one image (src) -> census image (dst), in DDR3. ----
void census_image(uint8_t* gmem, uint32_t srcOff, uint32_t dstOff,
                  int W, int H) {
    const int R = SGM_CENSUS_RADIUS;
    static uint8_t  lines[2 * SGM_CENSUS_RADIUS + 1][SGM_MAX_WIDTH];
    static uint64_t cenRow[SGM_MAX_WIDTH];

    // Preload the first (2R+1) rows.
    for (int i = 0; i < 2 * R + 1 && i < H; ++i)
        std::memcpy(lines[i], gmem + srcOff + (size_t)i * W, W);

ROWS: for (int y = 0; y < H; ++y) {
        // Slide the window so lines[] holds rows [y-R, y+R].
        if (y > R) {
            for (int i = 0; i < 2 * R; ++i)
                std::memcpy(lines[i], lines[i + 1], W);
            const int newRow = y + R;
            if (newRow < H)
                std::memcpy(lines[2 * R], gmem + srcOff + (size_t)newRow * W, W);
        }

        const bool rowValid = (y >= R && y < H - R);
    COLS: for (int x = 0; x < W; ++x) {
            uint64_t desc = 0;
            if (rowValid && x >= R && x < W - R) {
                const uint8_t center = lines[R][x];
                int shift = 0;
                const int total = (2 * R + 1) * (2 * R + 1);
            CY: for (int dy = 0; dy <= 2 * R; ++dy) {
                CX: for (int dx = -R; dx <= R; ++dx) {
                        if (shift != total / 2) {
                            desc <<= 1;
                            if (lines[dy][x + dx] < center) desc |= 1ull;
                        }
                        ++shift;
                    }
                }
            }
            cenRow[x] = desc;  // 0 on the border (matches CPU)
        }
        std::memcpy(gmem + dstOff + (size_t)y * W * 8, cenRow, (size_t)W * 8);
    }
}

// Compute the matching-cost vector for pixel (x,y) into cost[0..D).
// lcRow = left census row y; rcWin[dy+V] = right census row (y+dy), valid[] says
// which window rows are in range.
void matching_cost(const uint64_t lcRow[SGM_MAX_WIDTH],
                   const uint64_t rcWin[2 * SGM_MAX_VSEARCH + 1][SGM_MAX_WIDTH],
                   const bool rowValid[2 * SGM_MAX_VSEARCH + 1],
                   int x, int W, int D, int minD, int V,
                   uint8_t cost[SGM_MAX_DISP]) {
    const uint64_t leftDesc = lcRow[x];
DLOOP: for (int d = 0; d < D; ++d) {
        const int xr = x - (d + minD);
        if (xr < 0 || xr >= W) { cost[d] = kCostMax; continue; }
        int best = kCostMax;
    VS: for (int k = 0; k < 2 * V + 1; ++k) {
            if (!rowValid[k]) continue;
            const int h = popcount64(leftDesc ^ rcWin[k][xr]);
            if (h < best) best = h;
        }
        cost[d] = (uint8_t)(best < kCostMax ? best : kCostMax);
    }
}

// Load the right-census window rows [y-V, y+V] for row y from DDR3.
void load_right_window(uint8_t* gmem, int y, int W, int H, int V,
                       uint64_t rcWin[2 * SGM_MAX_VSEARCH + 1][SGM_MAX_WIDTH],
                       bool rowValid[2 * SGM_MAX_VSEARCH + 1]) {
WIN: for (int k = 0; k < 2 * V + 1; ++k) {
        const int yr = y + (k - V);
        if (yr < 0 || yr >= H) { rowValid[k] = false; continue; }
        rowValid[k] = true;
        std::memcpy(rcWin[k], gmem + SGM_OFF_CENSUS_R + (size_t)yr * W * 8,
                    (size_t)W * 8);
    }
}

} // namespace

// ===========================================================================
//  HLS top
// ===========================================================================
void sgm_census(volatile uint8_t* gmem_v) {
#pragma HLS INTERFACE m_axi port=gmem_v offset=slave bundle=gmem depth=268435456
#pragma HLS INTERFACE s_axilite port=gmem_v bundle=control
#pragma HLS INTERFACE s_axilite port=return bundle=control

    uint8_t* gmem = (uint8_t*)gmem_v;

    SgmHwParams p;
    std::memcpy(&p, gmem + SGM_OFF_PARAMS, sizeof(p));

    const int W    = p.width;
    const int H    = p.height;
    const int D    = p.numDisparities;
    const int V    = p.verticalSearchRange;
    const int minD = p.minDisparity > 0 ? p.minDisparity : 0;
    const int P1   = p.p1;
    const int P2   = p.p2;
    const int uniq = p.uniquenessRatio;          // 0 = disabled
    const bool subpix = p.enableSubpixel != 0;

    if (W <= 0 || H <= 0 || D <= 0 || W > SGM_MAX_WIDTH ||
        H > SGM_MAX_HEIGHT || D > SGM_MAX_DISP || V > SGM_MAX_VSEARCH)
        return;

    // 1) Census transforms -> DDR3.
    census_image(gmem, SGM_OFF_LEFT,  SGM_OFF_CENSUS_L, W, H);
    census_image(gmem, SGM_OFF_RIGHT, SGM_OFF_CENSUS_R, W, H);

    // Shared on-chip buffers.
    static uint64_t lcRow[SGM_MAX_WIDTH];
    static uint64_t rcWin[2 * SGM_MAX_VSEARCH + 1][SGM_MAX_WIDTH];
    static bool     rowValid[2 * SGM_MAX_VSEARCH + 1];
    static uint16_t prevRowVert[SGM_MAX_WIDTH][SGM_MAX_DISP]; // T->B (fwd) / B->T (bwd)
    uint16_t prevPix[SGM_MAX_DISP];
    uint8_t  cost[SGM_MAX_DISP];
    uint16_t lr[SGM_MAX_DISP], tb[SGM_MAX_DISP];
    uint16_t fwd[SGM_MAX_DISP];

    // 2) Forward pass: aggregate L->R + T->B, store forward aggregate to DDR3.
FWD_ROWS: for (int y = 0; y < H; ++y) {
        std::memcpy(lcRow, gmem + SGM_OFF_CENSUS_L + (size_t)y * W * 8, (size_t)W * 8);
        load_right_window(gmem, y, W, H, V, rcWin, rowValid);

    FWD_COLS: for (int x = 0; x < W; ++x) {
            matching_cost(lcRow, rcWin, rowValid, x, W, D, minD, V, cost);
            sgmStep(x == 0, cost, prevPix,         lr, D, P1, P2);  // L->R
            sgmStep(y == 0, cost, prevRowVert[x],  tb, D, P1, P2);  // T->B
        ACC: for (int d = 0; d < D; ++d) {
                fwd[d] = (uint16_t)(lr[d] + tb[d]);
                prevPix[d]        = lr[d];
                prevRowVert[x][d] = tb[d];
            }
            std::memcpy(gmem + SGM_OFF_FWDAGG + ((size_t)y * W + x) * D * 2,
                        fwd, (size_t)D * 2);
        }
    }

    // 3) Backward pass: aggregate R->L + B->T, add forward aggregate, WTA -> DISP.
    static int16_t dispRow[SGM_MAX_WIDTH];
    uint16_t rl[SGM_MAX_DISP], bt[SGM_MAX_DISP];
    uint32_t total[SGM_MAX_DISP];

BWD_ROWS: for (int y = H - 1; y >= 0; --y) {
        std::memcpy(lcRow, gmem + SGM_OFF_CENSUS_L + (size_t)y * W * 8, (size_t)W * 8);
        load_right_window(gmem, y, W, H, V, rcWin, rowValid);

    BWD_COLS: for (int x = W - 1; x >= 0; --x) {
            matching_cost(lcRow, rcWin, rowValid, x, W, D, minD, V, cost);
            sgmStep(x == W - 1, cost, prevPix,        rl, D, P1, P2);  // R->L
            sgmStep(y == H - 1, cost, prevRowVert[x], bt, D, P1, P2);  // B->T

            std::memcpy(fwd, gmem + SGM_OFF_FWDAGG + ((size_t)y * W + x) * D * 2,
                        (size_t)D * 2);

            uint32_t minC = 0xFFFFFFFFu, secC = 0xFFFFFFFFu;
            int bestD = 0;
        TOT: for (int d = 0; d < D; ++d) {
                const uint32_t t = (uint32_t)fwd[d] + rl[d] + bt[d];
                total[d] = t;
                if (t < minC)      { secC = minC; minC = t; bestD = d; }
                else if (t < secC) { secC = t; }
                prevPix[d]        = rl[d];
                prevRowVert[x][d] = bt[d];
            }

            // Uniqueness (uniq == 0 => disabled).
            bool unique = true;
            if (uniq > 0 && minC > 0) {
                const uint32_t margin = ((secC - minC) * 100u) / (minC + 1u);
                if ((int)margin < uniq) unique = false;
            }

            if (!unique) { dispRow[x] = 0; continue; }

            // Parabola subpixel (mirrors selectDisparitiesWTA()).
            float off = 0.0f;
            if (subpix && bestD > 0 && bestD < D - 1) {
                const float cp = (float)total[bestD - 1];
                const float cc = (float)total[bestD];
                const float cn = (float)total[bestD + 1];
                const float den = 2.0f * (cp - 2.0f * cc + cn);
                if (den > 1e-6f || den < -1e-6f) {
                    off = (cp - cn) / den;
                    if (off > 0.5f) off = 0.5f;
                    if (off < -0.5f) off = -0.5f;
                }
            }
            const float dsub = ((float)(bestD + minD) + off) * 16.0f;
            int di = (int)(dsub + (dsub >= 0 ? 0.5f : -0.5f));
            dispRow[x] = (int16_t)di;
        }
        std::memcpy(gmem + SGM_OFF_DISP + (size_t)y * W * 2, dispRow, (size_t)W * 2);
    }
}
