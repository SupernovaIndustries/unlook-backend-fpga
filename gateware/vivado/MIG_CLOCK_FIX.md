# MIG clock fix for the `xdma_ddr3` base design

`xdma_ddr3_bd.tcl` in this folder is the rigoorozco `m2-artix7-accelerator-card`
**`projects/xdma_ddr3/xdma_ddr3_bd.tcl`** block-design script **with our MIG-clock fix
applied**. Drop it in place of the stock one and rebuild (`full.tcl`) to get a bitstream
whose card-side AXI (BRAM + DDR3) actually works.

## Why this was needed
The stock design had a **MIG clocking deadlock** (confirmed in the built `xdma_ddr3.hwh`):

```
mig_7series_0/sys_clk_i  <- mig_7series_0/ui_clk     # MIG fed by its OWN output
mig_7series_0/clk_ref_i  <- mig_7series_0/ui_clk
axi_smc/aclk1            <- mig_7series_0/ui_clk      # SmartConnect 2nd clock = ui_clk
```

`System Clock = "No Buffer"`, so at power-on the MIG MMCM has **no input clock** → never
locks → `ui_clk` never starts → DDR3 never calibrates → the SmartConnect `aclk1` domain is
dead → **every XDMA bus-master transfer to BRAM (`0x0`) or DDR3 (`0x80000000`) hangs**
(engine BUSY, `completed_desc_count=0`, `ERESTARTSYS`). The AXI-Lite path (gpio, on XDMA's
own 125 MHz `axi_aclk`) was unaffected, which is how we isolated it. There was also a 2nd
deadlock: `mig/sys_rst <- rst_mig/interconnect_aresetn`, itself clocked by `ui_clk`.

This is NOT a Raspberry Pi problem — the host DMA window/driver were all correct.

## The fix (5 edits)
1. Add `xilinx.com:ip:clk_wiz:6.0` to the IP-check list.
2. Instantiate **`clk_wiz_mig`**: `PRIM_IN_FREQ 125`, `PRIM_SOURCE No_buffer`,
   `CLKOUT1_REQUESTED_OUT_FREQ 200`, `USE_LOCKED true`, `USE_RESET false`.
3. Remove `mig/sys_clk_i` + `mig/clk_ref_i` from the `mig_7series_0_ui_clk` net (leave it on
   `axi_smc/aclk1` + `mig/ui_clk` + `rst_mig/slowest_sync_clk`).
4. Add `clk_wiz_mig/clk_in1` to the `xdma_0_axi_aclk` net (feed it the 125 MHz).
5. Replace the circular `mig/sys_rst` net with:
   - `clk_wiz_mig/clk_out1` (200 MHz) → `mig/sys_clk_i` + `mig/clk_ref_i`
   - `clk_wiz_mig/locked` → `mig/sys_rst`

Result: a real 200 MHz system/reference clock derived from the PCIe-side `axi_aclk` (stable
after link-up); the MIG calibrates, `ui_clk` runs, the SmartConnect revives. **Verified on HW:
DDR3 DMA round-trips at ~190 MB/s.** See `docs/HARDWARE_BRINGUP.md` §F.1.

> Fallback if the 200 MHz off `axi_aclk` is too jittery for the IDELAYCTRL: source it from the
> 100 MHz PCIe refclk via `util_ds_buf` `IBUF_DS_ODIV2` instead of `axi_aclk`.

Upstream base design: <https://github.com/rigoorozco/m2-artix7-accelerator-card> (project
`xdma_ddr3`). Only the clocking above is changed; lanes/pins/MIG-params are theirs.
