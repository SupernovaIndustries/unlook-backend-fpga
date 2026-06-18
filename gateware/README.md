# Unlook SGM-Census Gateware (Artix-7 XC7A200T)

FPGA implementation of the Unlook SGM-Census stereo matcher, written in **Vitis HLS
(C++)** and packaged as an AXI IP that drops into the rigoorozco `xdma_ddr3`
reference design. Drop-in for the SDK CPU path: same disparity geometry (int16 ×16,
invalid == 0) so `reprojectImageTo3D(Q)` downstream is unchanged.

## Why HLS
The core is a faithful C++ port of `unlook-sdk/src/stereo/SGMCensus.cpp`, so parity
with the CPU is verifiable (csim/cosim vs the CPU golden) and the design is easy to
tweak/optimize on hardware with Claude Code. Hand-RTL only if a hot loop later
refuses to close timing.

## Files
- `hls/sgm_census.hpp` / `sgm_census.cpp` — the HLS top + stages: census 7×7
  (48-bit), Hamming cost with ±V vertical search, 4-path SGM (L→R, R→L, T→B, B→T)
  with P1/P2, WTA + uniqueness + parabola subpixel, int16 ×16 output. Single
  `gmem` AXI-master over DDR3, `ap_ctrl` on s_axilite.
- `hls/sgm_mem_layout.h` — DDR3 layout + `SgmHwParams` (shared with the host).
- `hls/tb_sgm_census.cpp` — C testbench: parity vs the CPU golden.
- `hls/run_hls.tcl` — csim → csynth → cosim → export IP.
- `vivado/create_project.tcl` — integrate the IP into `xdma_ddr3`, build bitstream
  + `.mcs`. `constraints/sgm.xdc` — SGM-core-specific timing/config only.

## Operating point (from calibration.yaml)
Frame 800×600, scan ROI **680×420**, `numDisparities` per working distance (default
128; Z=96848/d), **vertical search ±2** (mean epipolar 0.383 px / max 2.435 px → ±2
suffices; the CPU default ±8 is dropped). All are runtime params in `SgmHwParams`;
the on-chip buffer maxima are `SGM_MAX_*` in `sgm_census.hpp`.

## Architecture (correctness-first)
Two raster passes reproduce the CPU's 4 paths exactly: forward (L→R + T→B) stages a
per-pixel aggregate in DDR3; backward (R→L + B→T) sums it and runs WTA+subpixel
inline. Census images are precomputed to DDR3 and re-read windowed by both passes.
DDR3 scratch (`SGM_OFF_*`): census_L/R + forward aggregate ≈ 78 MB at 680×420/D=128
→ the card needs ≥ ~256 MB DDR3.

## Throughput (the iteration target)
This version prioritizes **parity**, not fps. The optimization levers (do them on
hardware once cosim parity passes): `#pragma HLS dataflow` across stages, on-chip
cost reuse to avoid the DDR3 forward-aggregate round-trip, `array_partition` on the
disparity dimension + `pipeline` on the d-loops, and stripe/tile the image. See the
NOTE blocks in `sgm_census.cpp`.

## Build
```
cd hls && vitis_hls -f run_hls.tcl              # verify + export IP (needs the .bin vectors)
cd ../vivado && vivado -mode batch -source create_project.tcl   # bitstream + .mcs
```
See `../RUNBOOK.md` for the end-to-end hardware procedure.
