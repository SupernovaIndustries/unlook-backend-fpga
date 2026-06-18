# RUNBOOK — bring up the Unlook FPGA backend end-to-end

Concrete, ordered procedure from source to a working accelerator on the CM5.
Two machines: the **Windows PC** (Vivado + JTAG programmer) builds/flashes the
gateware; the **CM5** (Linux/ARM64) runs the SDK and drives the card over PCIe.

## 0. Verify parity in simulation FIRST (no hardware)
On any Linux box with the SDK built+installed:
```
# build the SDK (branch feat/fpga-plugin-backend), then:
cmake -S unlook-backend-fpga -B build -DUNLOOK_FPGA_BUILD_TOOLS=ON \
      -DUnlookSDK_DIR=<sdk-install>/lib/cmake/UnlookSDK
cmake --build build
./build/tools/gen_golden left_rect.png right_rect.png 128 gateware/hls
```
Then in `gateware/hls`: `vitis_hls -f run_hls.tcl`. csim/cosim must report
`[tb] PASS` (≥99% within ±1 px). This proves the core is correct before silicon.
`run_hls.tcl` also exports the IP and prints the resource/timing estimate.

## 1. Build the bitstream (Windows PC, Vivado 2021.2)
- Check out the rigoorozco `m2-artix7-accelerator-card` repo; build the
  `xdma_ddr3` example once so its block-design tcl exists.
- Edit `gateware/vivado/create_project.tcl`: set `PART` to the exact XC7A200T
  package and `XDMA_DDR3_BD_TCL` to the example's BD tcl. Confirm the two SGM
  wiring lines against the example's cell names (XDMA AXI-Lite master, MIG AXI).
- `vivado -mode batch -source gateware/vivado/create_project.tcl`
  → produces the bitstream and `firmware/sgm_census_a200t_v1.mcs`.

## 2. Flash the SPI flash (Windows PC, JTAG) — persistent
Connect the JTAG programmer (USB→PC, ribbon→card JTAG header). In Vivado:
`Open Hardware Manager` → `Open Target` → `Auto Connect` → `Add Configuration
Memory Device` → **W25Q128** → program with the `.mcs`. The FPGA then auto-boots
this gateware on every power-up.
- ⚠️ For a quick volatile test instead, `Program Device` with the `.bit` (lost on
  power-off).
- ⚠️ **PCIe ordering**: program the flash with the **CM5 powered off**, then boot
  the CM5 — a PCIe endpoint must be configured before the host enumerates it.

## 3. XDMA driver (CM5, once)
```
git clone https://github.com/Xilinx/dma_ip_drivers
cd dma_ip_drivers/XDMA/linux-kernel/xdma && make && sudo make install
sudo modprobe xdma            # or insmod ./xdma.ko
ls /dev/xdma0_*               # expect _user _h2c_0 _c2d_0
lspci | grep -i 10ee          # expect the Xilinx endpoint
```
(Building an out-of-tree module on the Pi kernel needs the matching kernel
headers; if the module won't build/load, the SDK simply runs on CPU.)

## 4. Run the SDK with the FPGA backend (CM5)
Build + install `libunlook_fpga_backend.so`:
```
cmake -S unlook-backend-fpga -B build && cmake --build build && sudo cmake --install build
ctest --test-dir build --output-on-failure      # abi_selftest: probe/create/compute
```
The SDK's `StereoBackendType::Auto` now finds the plugin + the XC7A200T and uses
it; otherwise it logs a CPU fallback. Compare the disparity/point cloud against the
CPU path (same Q, `Z=96848/d`).

## 5. Optimize (iterative, on the Pi with Claude Code)
Once correct, raise throughput via the HLS pragmas in `gateware/hls/sgm_census.cpp`
(dataflow, on-chip cost reuse, partition+pipeline on D, tiling). Rebuild IP →
bitstream → reflash. Bump the core version + `firmware/` provenance on each release.
