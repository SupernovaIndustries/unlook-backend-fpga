/**
 * @file gen_golden.cpp
 * @brief Produce reference vectors for the HLS testbench from the CPU SGMCensus.
 *
 * Runs unlook::stereo::SGMCensus on a rectified grayscale pair with the SAME
 * parameters the FPGA core uses (4 paths, vertical +/-2, no median, subpixel),
 * and dumps: params.bin (SgmHwParams), left.bin / right.bin (uint8 W*H),
 * disp_golden.bin (int16 W*H, x16). Drop these next to run_hls.tcl for csim/cosim.
 *
 * Usage: gen_golden <left_rect.png> <right_rect.png> [numDisparities=128] [outdir=.]
 *
 * The inputs must already be RECTIFIED (e.g. the 680x420 scan ROI). The CPU
 * config mirrors the production preset so FPGA<->CPU parity is meaningful.
 */

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

#include <unlook/stereo/SGMCensus.hpp>

#include "sgm_mem_layout.h"   // SgmHwParams (shared host/core layout)

static void dump(const std::string& path, const void* data, size_t bytes) {
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) { std::fprintf(stderr, "cannot write %s\n", path.c_str()); std::exit(2); }
    std::fwrite(data, 1, bytes, f);
    std::fclose(f);
}

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr,
            "usage: %s <left_rect> <right_rect> [numDisparities=128] [outdir=.]\n", argv[0]);
        return 1;
    }
    const std::string leftPath = argv[1], rightPath = argv[2];
    const int numDisp = (argc > 3) ? std::atoi(argv[3]) : 128;
    const std::string outdir = (argc > 4) ? argv[4] : ".";

    cv::Mat l = cv::imread(leftPath,  cv::IMREAD_GRAYSCALE);
    cv::Mat r = cv::imread(rightPath, cv::IMREAD_GRAYSCALE);
    if (l.empty() || r.empty() || l.size() != r.size()) {
        std::fprintf(stderr, "failed to load equal-size grayscale inputs\n");
        return 1;
    }

    // Config = exactly what the FPGA core implements (see sgm_census.cpp).
    unlook::stereo::SGMCensus::Config cfg;
    cfg.censusWindowSize     = 7;
    cfg.minDisparity         = 0;
    cfg.numDisparities       = numDisp;
    cfg.verticalSearchRange  = 2;
    cfg.P1                   = 8;
    cfg.P2                   = 32;
    cfg.use8Paths            = false;   // 4 paths: L->R, R->L, T->B, B->T
    cfg.enableUniquenessCheck = false;  // production preset; set true to test uniqueness
    cfg.uniquenessRatio      = 15;
    cfg.enableMedianFilter   = false;   // the core does NOT median-filter
    cfg.enableSubpixel       = true;

    unlook::stereo::SGMCensus sgm(cfg);
    const auto res = sgm.compute(l, r);
    if (!res.success || res.disparity.type() != CV_16SC1) {
        std::fprintf(stderr, "CPU SGMCensus failed: %s\n", res.errorMessage.c_str());
        return 1;
    }

    SgmHwParams p;
    std::memset(&p, 0, sizeof(p));
    p.width = l.cols; p.height = l.rows;
    p.minDisparity = cfg.minDisparity; p.numDisparities = cfg.numDisparities;
    p.verticalSearchRange = cfg.verticalSearchRange;
    p.censusWindowSize = cfg.censusWindowSize;
    p.p1 = cfg.P1; p.p2 = cfg.P2;
    p.uniquenessRatio = cfg.enableUniquenessCheck ? cfg.uniquenessRatio : 0;
    p.enableSubpixel = cfg.enableSubpixel ? 1 : 0;

    cv::Mat lc = l.isContinuous() ? l : l.clone();
    cv::Mat rc = r.isContinuous() ? r : r.clone();
    cv::Mat dc = res.disparity.isContinuous() ? res.disparity : res.disparity.clone();

    dump(outdir + "/params.bin",      &p, sizeof(p));
    dump(outdir + "/left.bin",        lc.data, (size_t)lc.cols * lc.rows);
    dump(outdir + "/right.bin",       rc.data, (size_t)rc.cols * rc.rows);
    dump(outdir + "/disp_golden.bin", dc.data, (size_t)dc.cols * dc.rows * 2);

    std::printf("wrote params/left/right/disp_golden for %dx%d D=%d to %s\n",
                l.cols, l.rows, numDisp, outdir.c_str());
    return 0;
}
