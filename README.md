# unlook-backend-fpga

FPGA stereo accelerator for [Unlook](../unlook-sdk) — offloads the SGM-Census
disparity computation to an **Artix-7 XC7A200T** (M.2, PCIe/XDMA) so the scanning
pipeline runs in real time (target 60 fps) instead of ~2-3 s/frame on the CPU.

Everything FPGA-specific lives here so the SDK stays clean: the SDK contains **no**
XDMA/PCIe/gateware code. It `dlopen`s `libunlook_fpga_backend.so` (built here) at
runtime through a stable C ABI and selects it via `StereoBackendType::Auto`,
falling back to the CPU SGMCensus path when no FPGA is present.

## Contract
The host↔backend boundary is the C ABI in
[`include/unlook_fpga_backend.h`](include/unlook_fpga_backend.h) — kept **byte-identical**
with the SDK's copy (`unlook-sdk/include/unlook/fpga/unlook_fpga_backend.h`).
`UNLOOK_FPGA_ABI_VERSION` guards mismatches at load time. Disparity is returned as
**int16 scaled ×16** (invalid == 0), exactly the SGMCensus layout, so the SDK's Q
matrix and `reprojectImageTo3D` are unchanged.

## Layout
```
include/   C ABI header (the contract, synced with the SDK)
host/      libunlook_fpga_backend.so (real XDMA backend)
  xdma/    XdmaTransport (mmap BAR + H2C/C2D DMA), FpgaDetector, RegisterMap,
           xdma_backend (the C ABI: params+images->DDR3, ap_start, poll, readback)
gateware/  Vitis HLS SGM-Census core + Vivado integration (see gateware/README.md)
firmware/  released, versioned bitstreams (see firmware/README.md)
tools/     gen_golden -- CPU reference vectors for the HLS parity test
tests/     abi_selftest -- dlopen the plugin and exercise probe/create/compute
```

## Build (host plugin, on the CM5)
```bash
cmake -S . -B build
cmake --build build -j
ctest --test-dir build --output-on-failure   # abi_selftest: SKIPs cleanly with no FPGA
sudo cmake --install build
```
The SDK's `StereoBackendType::Auto` resolves `libunlook_fpga_backend.so`, and when an
Unlook XC7A200T is present on PCIe it offloads SGM-Census to it; otherwise CPU fallback.
Override the location via `scan.fpga.libraryPath` / `[fpga] plugin_path`.

The gateware (HLS core + bitstream) is built separately — see
[`gateware/README.md`](gateware/README.md) and the end-to-end [`RUNBOOK.md`](RUNBOOK.md).

## Status
- ✅ C ABI + real XDMA host backend + HLS SGM-Census core + parity testbench + Vivado
  integration script + self-test.
- ⏳ Hardware bring-up (CM5): build the Xilinx `xdma` kernel module on ARM64, flash the
  bitstream, confirm PCIe enumeration; then verify parity on silicon and tune throughput.

Proprietary — © 2026 Supernova Industries S.r.l. See [LICENSE](LICENSE).
