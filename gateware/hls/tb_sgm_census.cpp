/**
 * @file tb_sgm_census.cpp
 * @brief C testbench for the HLS sgm_census core (csim + cosim).
 *
 * Loads a rectified pair + the CPU "golden" disparity produced by
 * tools/gen_golden (run with the SAME parameters), stages them in a simulated
 * DDR3 buffer, runs the core, and checks parity.
 *
 * Files (in the working dir, see run_hls.tcl): params.bin (SgmHwParams),
 * left.bin / right.bin (uint8 W*H), disp_golden.bin (int16 W*H, x16).
 *
 * Pass criterion: >= 99% of pixels match within +/- 1 (x16 unit = 1/16 px),
 * counting only pixels where the golden is valid (!= 0).
 */

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <cmath>
#include <vector>

#include "sgm_census.hpp"

static std::vector<uint8_t> readFile(const char* path, size_t expect) {
    FILE* f = std::fopen(path, "rb");
    if (!f) { std::fprintf(stderr, "cannot open %s\n", path); std::exit(2); }
    std::vector<uint8_t> buf(expect);
    const size_t n = std::fread(buf.data(), 1, expect, f);
    std::fclose(f);
    if (n != expect) { std::fprintf(stderr, "%s: short read %zu/%zu\n", path, n, expect); std::exit(2); }
    return buf;
}

int main() {
    SgmHwParams p;
    {
        auto pb = readFile("params.bin", sizeof(SgmHwParams));
        std::memcpy(&p, pb.data(), sizeof(p));
    }
    const int W = p.width, H = p.height, D = p.numDisparities;
    std::printf("[tb] %dx%d D=%d V=%d census=%d P1=%d P2=%d uniq=%d subpix=%d\n",
                W, H, D, p.verticalSearchRange, p.censusWindowSize, p.p1, p.p2,
                p.uniquenessRatio, p.enableSubpixel);

    const size_t img  = (size_t)W * H;
    const size_t total = (size_t)SGM_OFF_FWDAGG + img * D * 2 + 4096;
    std::vector<uint8_t> ddr(total, 0);

    std::memcpy(ddr.data() + SGM_OFF_PARAMS, &p, sizeof(p));
    auto left  = readFile("left.bin",  img);
    auto right = readFile("right.bin", img);
    std::memcpy(ddr.data() + SGM_OFF_LEFT,  left.data(),  img);
    std::memcpy(ddr.data() + SGM_OFF_RIGHT, right.data(), img);

    sgm_census((volatile uint8_t*)ddr.data());

    auto goldenB = readFile("disp_golden.bin", img * 2);
    const int16_t* golden = (const int16_t*)goldenB.data();
    const int16_t* dut    = (const int16_t*)(ddr.data() + SGM_OFF_DISP);

    long long checked = 0, ok = 0, maxAbs = 0;
    for (size_t i = 0; i < img; ++i) {
        if (golden[i] == 0) continue;     // compare on golden-valid pixels
        ++checked;
        const int diff = std::abs((int)dut[i] - (int)golden[i]);
        if (diff > maxAbs) maxAbs = diff;
        if (diff <= 16) ++ok;             // within +/- 1.0 px (x16)
    }
    const double pct = checked ? 100.0 * (double)ok / (double)checked : 0.0;
    std::printf("[tb] parity: %lld/%lld within +/-1px (%.2f%%), maxAbs=%lld (x16)\n",
                ok, checked, pct, maxAbs);

    if (checked == 0) { std::printf("[tb] FAIL: no valid golden pixels\n"); return 1; }
    if (pct < 99.0)   { std::printf("[tb] FAIL: parity below 99%%\n"); return 1; }
    std::printf("[tb] PASS\n");
    return 0;
}
