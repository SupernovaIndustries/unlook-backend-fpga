/*
 * abi_selftest.c -- dlopen the built libunlook_fpga_backend.so and exercise the
 * full C ABI (probe -> create -> compute -> destroy) on a synthetic stereo pair.
 *
 * On a host WITHOUT the FPGA present, probe() reports not-present and the test
 * cleanly SKIPs (still passes) -- so it is safe to run anywhere. With the card
 * present it exercises the full probe -> create -> compute path and checks a
 * non-trivial disparity comes back.
 */
#include <dlfcn.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "unlook_fpga_backend.h"

#ifndef UNLOOK_FPGA_LIB_PATH
#define UNLOOK_FPGA_LIB_PATH "libunlook_fpga_backend.so"
#endif

typedef uint32_t (*abi_ver_fn)(void);
typedef int      (*probe_fn)(const char*, UnlookFpgaInfo*);
typedef void*    (*create_fn)(const UnlookSgmParams*);
typedef int      (*compute_fn)(void*, const uint8_t*, const uint8_t*,
                               int32_t, int32_t, int16_t*, UnlookFpgaStats*);
typedef void     (*destroy_fn)(void*);

int main(void) {
    const char* libPath = getenv("UNLOOK_FPGA_LIB");
    if (!libPath) libPath = UNLOOK_FPGA_LIB_PATH;

    void* h = dlopen(libPath, RTLD_NOW | RTLD_LOCAL);
    if (!h) { fprintf(stderr, "dlopen(%s) failed: %s\n", libPath, dlerror()); return 1; }

    abi_ver_fn abiVer  = (abi_ver_fn) dlsym(h, "unlook_fpga_plugin_abi_version");
    probe_fn   probe   = (probe_fn)   dlsym(h, "unlook_fpga_probe");
    create_fn  create  = (create_fn)  dlsym(h, "unlook_fpga_create");
    compute_fn compute = (compute_fn) dlsym(h, "unlook_fpga_compute");
    destroy_fn destroy = (destroy_fn) dlsym(h, "unlook_fpga_destroy");
    if (!abiVer || !probe || !create || !compute || !destroy) {
        fprintf(stderr, "missing C ABI symbol\n"); return 1;
    }

    if (abiVer() != UNLOOK_FPGA_ABI_VERSION) {
        fprintf(stderr, "ABI mismatch: plugin %u != header %u\n",
                abiVer(), UNLOOK_FPGA_ABI_VERSION);
        return 1;
    }
    printf("[ok] ABI version %u\n", abiVer());

    UnlookFpgaInfo info;
    if (!probe("/dev/xdma0", &info) || !info.present) {
        printf("[skip] no FPGA backend present (%s) -- ABI checks passed\n", info.detail);
        dlclose(h);
        return 0;  /* clean skip: e.g. xdma backend with no hardware */
    }
    printf("[ok] probe: %s (vendor 0x%04x, version %u)\n",
           info.detail, info.pciVendorId, info.bitstreamVersion);

    const int32_t W = 64, H = 48;
    UnlookSgmParams p;
    memset(&p, 0, sizeof(p));
    p.imageWidth = W; p.imageHeight = H;
    p.minDisparity = 0; p.numDisparities = 64; p.verticalSearchRange = 2;
    p.censusWindowSize = 7; p.p1 = 8; p.p2 = 32; p.enableSubpixel = 1;
    strncpy(p.devicePath, "/dev/xdma0", sizeof(p.devicePath) - 1);

    void* inst = create(&p);
    if (!inst) { fprintf(stderr, "create() failed\n"); dlclose(h); return 1; }

    uint8_t* left  = (uint8_t*) malloc((size_t)W * H);
    uint8_t* right = (uint8_t*) malloc((size_t)W * H);
    int16_t* disp  = (int16_t*) malloc((size_t)W * H * sizeof(int16_t));
    for (int i = 0; i < W * H; ++i) { left[i] = (uint8_t)(40 + (i % 200)); right[i] = left[i]; }

    UnlookFpgaStats st;
    const int rc = compute(inst, left, right, W, H, disp, &st);
    if (rc != 0) {
        fprintf(stderr, "compute() rc=%d: %s\n", rc, st.errorMessage);
        free(left); free(right); free(disp); destroy(inst); dlclose(h); return 1;
    }

    int nonzero = 0;
    for (int i = 0; i < W * H; ++i) if (disp[i] != 0) ++nonzero;
    printf("[ok] compute: %d valid px (%.1f%%), %.3f ms, usedFpga=%d, nonzero=%d\n",
           st.validPixels, st.validPercent, st.totalTimeMs, st.usedFpga, nonzero);

    int ret = 0;
    if (nonzero == 0) { fprintf(stderr, "disparity is all-zero\n"); ret = 1; }

    free(left); free(right); free(disp);
    destroy(inst);
    dlclose(h);
    printf(ret == 0 ? "[PASS]\n" : "[FAIL]\n");
    return ret;
}
