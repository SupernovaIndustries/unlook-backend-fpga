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
host/      libunlook_fpga_backend.so
  mock/    synthetic-disparity backend (default; no hardware) -- the test fixture
  xdma/    real Xilinx XDMA backend (XdmaTransport, FpgaDetector, RegisterMap)
gateware/  HDL / Vivado project for the SGM-Census core (see gateware/README.md)
firmware/  released, versioned bitstreams (see firmware/README.md)
tests/     abi_selftest -- dlopen the plugin and exercise probe/create/compute
```

## Build
```bash
# Mock (default): builds + tests on any Linux host, no FPGA needed.
cmake -S . -B build -DUNLOOK_FPGA_BACKEND=mock
cmake --build build -j
ctest --test-dir build --output-on-failure        # runs abi_selftest

# Real hardware (Raspberry CM4/CM5 + XC7A200T + Xilinx xdma kernel module):
cmake -S . -B build-xdma -DUNLOOK_FPGA_BACKEND=xdma
cmake --build build-xdma -j
```

Point the SDK at the result (default search resolves `libunlook_fpga_backend.so`;
or set `scan.fpga.libraryPath` / the `[fpga] plugin_path` config key to an
absolute path). With the **mock** built and discoverable, the SDK's `Auto`/`Fpga`
backend produces a synthetic disparity end-to-end — validating the whole plugin
path with no Artix-7 attached.

## Status
- ✅ C ABI + mock backend + XDMA host skeleton + self-test.
- ⏳ Gateware (HDL) — see [`gateware/README.md`](gateware/README.md) milestones.
- ⏳ Fase 0 de-risk: confirm PCIe Root Complex on the CM4/CM5 carrier and build
  the Xilinx `xdma` kernel module on ARM64.

Proprietary — © 2026 Supernova Industries S.r.l. See [LICENSE](LICENSE).
