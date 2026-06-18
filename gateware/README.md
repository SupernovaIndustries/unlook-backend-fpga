# Unlook SGM-Census Gateware (Artix-7 XC7A200T)

FPGA implementation of the Unlook SGM-Census stereo matcher. Drop-in accelerator
for the SDK CPU path: same disparity geometry (int16 ×16, invalid == 0) so
`reprojectImageTo3D(Q)` downstream is unchanged.

## Target & interface
- **Device**: Xilinx Artix-7 **XC7A200T** (M.2 card; DDR3 via MIG, config flash).
  Budget: ~215K logic cells, ~269K LUT, 740 DSP48, ~13 Mb BRAM.
- **Host link**: PCIe x1 Gen2 via the **XDMA** IP.
  - AXI-Lite (`_user` BAR) → control/status register file (see
    [`../host/xdma/RegisterMap.hpp`](../host/xdma/RegisterMap.hpp) — the single
    source of truth for offsets, the `UNLK` ID magic, and `kCoreVersion`).
  - H2C → upload rectified L/R 8-bit images to card buffers.
  - C2D → return the int16 ×16 disparity map.
- **Operating point** (from `calibration.yaml`): frame 800×600, scan ROI
  **680×420**, `numDisparities` per working distance (128–256), **vertical
  search ±2** (mean epipolar 0.383 px / max 2.435 px → ±2 suffices; the CPU
  default ±8 is intentionally dropped on FPGA).

## Streaming pipeline (dataflow)
1. **Ingest** — H2C into BRAM line buffers (or DDR3 staging).
2. **Census transform** 7×7 (48-bit) — 7-row line buffer, 1 descriptor/clk.
3. **Matching cost** — Hamming (XOR + popcount) over `numDisparities`, with the
   ±`VSEARCH` row min. Dominant in LUTs; tune the parallel disparity-unit count.
4. **SGM aggregation** — start with **4 forward paths** (L→R, T→B, 2 diagonals)
   that stream naturally; backward paths need a second pass / DDR3 frame buffer.
   uint16 path cost, P1/P2 from registers, uint32 aggregate.
5. **WTA + uniqueness + parabola subpixel** → disparity int16 ×16 (exact replica
   of the CPU formula — this is the correctness contract).
6. **Egress** — C2D the disparity map.

## Milestones
- [ ] M1 PCIe/XDMA bring-up + AXI-Lite register file + C2D/H2C loopback
      (validates the host path against `../host` with a trivial core).
- [ ] M2 Census transform (line-buffered).
- [ ] M3 Matching cost (Hamming + vertical search).
- [ ] M4 SGM aggregation (4 paths), WTA + subpixel, ×16 output.
- [ ] M5 Numeric parity vs CPU SGMCensus on reference rectified pairs.
- [ ] M6 Timing closure @ target clock, 60 fps on the ROI; utilization report.

## Layout
- `rtl/`         — Verilog/SystemVerilog (or HLS) sources.
- `constraints/` — `.xdc` (pinout, clocks, timing). The current rigoorozco card
  has a **reversed PCIe lane order** — capture that here.
- `sim/`         — testbenches + reference vectors (golden disparity from CPU).
- `vivado/`      — `create_project.tcl` (regenerates the `.xpr`; the project
  itself is git-ignored). Vivado 2021.2 / 2025.x.

## Build (once RTL exists)
```
cd vivado && vivado -mode batch -source create_project.tcl
# ... synth/impl/bitstream ...
# copy the bitstream to ../firmware/ as a versioned release (see firmware/README.md)
```
