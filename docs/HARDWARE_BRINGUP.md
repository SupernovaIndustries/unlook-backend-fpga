# Unlook FPGA — Hardware bring-up runbook (Artix-7 XC7A200T M.2 on Raspberry CM5)

End-to-end, **hard-won** procedure to bring up the Artix-7 M.2 accelerator card on a
Raspberry CM5 over PCIe/XDMA, including every gotcha we hit. Reproduce anywhere.

> Target board: cheap M.2 "XC7A200T-ddr" card (rigoorozco/m2-artix7-accelerator-card),
> Winbond **W25Q128JV** SPI flash, Micron **MT41J128M16** DDR3 (256 MB), XDMA over PCIe.
> Host: Raspberry **CM5** (BCM2712), external PCIe controller `1000110000` (bus 0001).

## Address map (from the xdma_ddr3 block design)
- **DDR3 (MIG)** → `0x8000_0000`, range `0x1000_0000` (256 MB) on XDMA `M_AXI`.
- **AXI-Lite** (axi_gpio / user regs) → `0x4000_0000` on XDMA `M_AXI_LITE`.
- **BRAM** → `0x0000_0000`, 8 KB on `M_AXI`.
- PCIe device id: **10ee:7024**. Link negotiates **Gen1 x1 (2.5 GT/s)** on the Pi M.2
  (~200 MB/s usable — enough for 800×600@60fps ≈ 115 MB/s; force Gen2 later if needed).

For our `sgm_census` core: `m_axi gmem` → DDR3 @ `0x8000_0000` (set `kDdr3Base=0x80000000`
in `host/xdma/RegisterMap.hpp`), `s_axilite` ← XDMA AXI-Lite @ `0x4000_0000`.

---

## Status — what works today (2026-06-19)
| Piece | State | Notes |
|---|---|---|
| Bitstream build (Vivado 2021.1) | ✅ | reversed lanes, short path, **MIG-clock fix** (§F.1) |
| Flash programming (W25Q128, SPIx4) | ✅ | one-time, via JTAG HS2 (§B) |
| **FPGA auto-boot from flash** | ✅ | cold power-cycle → DONE/blue LED, **no JTAG at runtime** (§C) |
| PCIe enumeration (`10ee:7024`) | ✅ | needs a `brcm-pcie` rebind after the FPGA is ready (§D) |
| XDMA driver (ARM64, poll_mode + patches) | ✅ | `/dev/xdma0_*` nodes (§E) |
| **DDR3 bus-master DMA** | ✅ | round-trip `cmp` OK @ **~190 MB/s** (Gen1 x1) (§F, §F.1) |
| **Fully autonomous boot** | ✅ | systemd `unlook-fpga.service` → power-on → ready, **zero manual steps / zero flasher** (§G); verified after `reboot` |
| `sgm_census` HLS core integrated | ⏳ | NEXT (Fase 2): export IP → BD (gmem→DDR3, s_axilite→XDMA) → rebuild → SDK parity vs CPU |

**The single biggest blocker (DMA hang) was the stock bitstream's broken MIG clock, not the Pi — see §F.1.**

---

## A. Build the base bitstream (Windows PC, Vivado 2021.1)
Gotchas (all real, all hit):
1. **Short build path** — Windows 260-char limit; the MIG generates very deep paths.
   Build from `C:\m2` (copy the repo there) or `subst X: <repo>`.
2. **Run in YOUR interactive shell**, NOT a background/headless process. Headless can't
   spawn the OOC child processes → `[Common 17-232] Could not create slave interpreter`.
3. **Reversed PCIe lanes**: build the project **twice** — the "early" lane constraints
   only take on the 2nd build. Verify after impl (`open_run impl_1`):
   ```tcl
   get_property LOC [get_cells {xdma_ddr3_i/xdma_0/inst/xdma_ddr3_xdma_0_0_pcie2_to_pcie3_wrapper_i/pcie2_ip_i/inst/inst/gt_top_i/pipe_wrapper_i/pipe_lane[3].gt_wrapper_i/gtp_channel.gtpe2_channel_i}]
   ```
   must return **`GTPE2_CHANNEL_X0Y7`**.
4. Build: `vivado -mode batch -source ..\..\scripts\tcl\full.tcl -tclargs 2 4`
   (or GUI → Generate Bitstream). Output bit: `...\impl_1\xdma_ddr3_wrapper.bit`.

## B. Flash image + programming (Vivado Hardware Manager)
1. **Add the W25Q128 to Vivado's part table** (absent by default): append the
   "before 2023" line from `docs/Miscellaneous/winbond-flash-notes.txt` to
   `C:\Xilinx\Vivado\2021.1\data\xicom\xicom_cfgmem_part_table.csv`, then **restart Vivado**.
2. The bitstream boots SPI **x4** (`SPI_BUSWIDTH=4`, `CONFIGRATE=50`). Make the `.mcs`:
   ```tcl
   write_cfgmem -force -format mcs -size 16 -interface SPIx4 \
     -loadbit {up 0x0 <impl>/xdma_ddr3_wrapper.bit} C:/m2/out.mcs
   ```
   (a raw `write_bitstream -bin` is NOT a valid SPI-boot image — use `write_cfgmem`.)
3. Hardware Manager → Auto Connect → right-click `xc7a200t_0` →
   **Add/Program Configuration Memory Device** → `w25q128bv-spi-x1_x2_x4` → `out.mcs`.

## C. Configure the FPGA — it DOES auto-boot from flash (cold power-cycle)
The card's mode pins are strapped **M[2:0]=001 (Master SPI)**, so on a **clean cold
power-cycle** the FPGA self-configures from the W25Q128 flash (**blue LED / DONE on**
within ~150 ms). **The JTAG flasher is only needed once, to program the flash** (step B) —
NOT to boot. (Verified: power-cycle with the flasher unplugged → blue LED on → after the
PCIe rebind in step D the device enumerates.)
- Earlier we wrongly thought it needed JTAG every boot — that was the PCIe link timing
  (step D), not a config failure.
- Gotcha (cheap board): a **warm `sudo reboot`** may leave it unconfigured (power-sequencing);
  per UG470, a failed SPI config only resynchronizes by pulsing PROGRAM_B. A **cold power-cycle**
  is reliable. If you ever need to force a reload with the flasher attached: Hardware Manager →
  right-click `xc7a200t_0` → **Boot from Configuration Memory Device**. Definitive fix
  (clean power ramp + sequenced PROGRAM_B) belongs on our own carrier.

## D. Bring up the PCIe link on the CM5
PCIe trains at CM5 boot — before the FPGA is configured — so it starts `link down`.
After configuring the FPGA (step C), force the controller to re-train (safe: this
controller has no other device):
```bash
echo 1000110000.pcie | sudo tee /sys/bus/platform/drivers/brcm-pcie/unbind
sleep 1
echo 1000110000.pcie | sudo tee /sys/bus/platform/drivers/brcm-pcie/bind
dmesg | grep -iE "link up|link down" | tail -3      # expect: link up, 2.5 GT/s PCIe x1
lspci -nn | grep -i 10ee                            # 0001:01:00.0 [10ee:7024]
```

## E. XDMA kernel driver (CM5, ARM64)
Source: `m2-artix7-accelerator-card/app/xdma-kernel-module/xdma` (Xilinx XDMA v2020.2.2).
Three fixes are required for this device + the Pi:
1. **Device id 0x7024** is not in the driver's PCI table → bind manually with `new_id`.
2. **`poll_mode=1`** — the Pi PCIe MSI/MSI-X path fails; use polling.
3. **Two source patches** (the driver still touches MSI/MSI-X + user-ISR even in poll mode):
   ```bash
   cd app/xdma-kernel-module/xdma
   # skip user-interrupt enable (we have none; IP built with interrupt_pin NONE)
   sed -i 's/rv = xdma_user_isr_enable(hndl, mask);/rv = 0;/' xdma_mod.c
   # skip MSI-X + IRQ setup entirely when poll_mode is set
   sed -i 's/rv = enable_msi_msix(xdev, pdev);/rv = poll_mode ? 0 : enable_msi_msix(xdev, pdev);/' libxdma.c
   sed -i 's/rv = irq_setup(xdev, pdev);/rv = poll_mode ? 0 : irq_setup(xdev, pdev);/' libxdma.c
   ```
   Build + load + bind:
   ```bash
   make clean && make
   sudo rmmod xdma 2>/dev/null
   sudo insmod xdma.ko poll_mode=1
   echo "10ee 7024" | sudo tee /sys/bus/pci/drivers/xdma/new_id
   ls -l /dev/xdma*     # expect: xdma0_user, xdma0_h2c_0, xdma0_c2h_0, xdma0_control ...
   ```

## F. Verify the DMA datapath (DDR3)
With `/dev/xdma0_*` present, use the repo tools (`app/xdma-tools`) to write/read DDR3:
```bash
# write 1 MB of random data to DDR3 @ 0x80000000, read it back, compare
dd if=/dev/urandom of=/tmp/tx.bin bs=1M count=1
./dma_to_device   -d /dev/xdma0_h2c_0 -a 0x80000000 -s 0x100000 -f /tmp/tx.bin
./dma_from_device -d /dev/xdma0_c2h_0 -a 0x80000000 -s 0x100000 -f /tmp/rx.bin
cmp /tmp/tx.bin /tmp/rx.bin && echo "DDR3 DMA OK"
```
AXI-Lite registers (our core's ap_ctrl, once the SGM IP is integrated) are reached by
mmap-ing `/dev/xdma0_user` at the AXI-Lite offset.

## F.1 ⚠️ The stock `xdma_ddr3` bitstream has a BROKEN MIG clock — DMA hangs until fixed
**Symptom:** `/dev/xdma0_*` all present, PCIe link up, AXI-Lite registers read fine, but **every**
bus-master transfer (`dma_to_device`/`dma_from_device` to BRAM@`0x0` *or* DDR3@`0x80000000`) times
out: engine `BUSY`, `completed_desc_count=0`, `ERESTARTSYS (512)`, `transfer_abort ... desc 1`.

**This is NOT a Pi problem.** Proof it's the FPGA, not the host:
- `IB MEM 0x0..0x7fffffff -> 0x0` (2 GB identity) with `dtoverlay=pcie-32bit-dma-pi5` — the H2C
  descriptor lands at `first_desc_lo` inside that window (`first_desc_hi=0`). Host DMA window is fine.
- `reg_rw /dev/xdma0_control 0x0 w` → `0x1fc00006` (XDMA channel id) — downstream PCIe/BAR solid.
- `reg_rw /dev/xdma0_user 0x0 w [0x7|0x0]` → reads/writes the `axi_gpio` (3 LEDs) on **M_AXI_LITE**
  with **no hang** — that path runs on XDMA's own 125 MHz `axi_aclk`, independent of the MIG.
- Only the **M_AXI** path (BRAM + DDR3, both behind the `axi_smc` SmartConnect) hangs.

**Root cause (confirmed in `xdma_ddr3.hwh`):** the SmartConnect's 2nd clock is `aclk1 = mig ui_clk`,
and the MIG clocking is a **deadlock** — `mig/sys_clk_i` and `mig/clk_ref_i` (inputs) are wired to
`mig/ui_clk` (the MIG's own *output*; System Clock = "No Buffer"). With no external clock the MMCM
never locks → DDR3 never calibrates → `ui_clk` never starts → the SmartConnect `aclk1` domain is dead
→ all card-side AXI hangs. (Second deadlock: `mig/sys_rst` ← `rst_mig/interconnect_aresetn`, itself on
`ui_clk`.) `init_calib_complete` is left unconnected, so you can't read calibration from software.

**Fix (in `projects/xdma_ddr3/xdma_ddr3_bd.tcl`, the bd the build sources):** add a Clocking Wizard
`clk_wiz_mig` (in = `xdma_0/axi_aclk` 125 MHz, out = **200 MHz**, `USE_LOCKED`, no reset); drive
`mig/sys_clk_i` **and** `mig/clk_ref_i` from its 200 MHz output; drive `mig/sys_rst` from its `locked`;
leave the `ui_clk` net only on `axi_smc/aclk1` + `ui_clk` + `rst_mig/slowest_sync_clk`. Now DDR3 calibrates
(a few ms after PCIe link-up, when `axi_aclk` is live), `ui_clk` runs, the SmartConnect revives, DMA works.
Fallback if the IDELAYCTRL 200 MHz is too jittery off `axi_aclk`: source it from the 100 MHz PCIe refclk
via `util_ds_buf` `IBUF_DS_ODIV2` instead. Rebuild: `vivado -mode batch -source ..\..\scripts\tcl\full.tcl
-tclargs 2 4` (in your own interactive shell), reflash the `.mcs`, repeat step F — it must pass now.

**✅ VERIFIED (2026-06-19):** after rebuild+reflash, `dma_to_device`/`dma_from_device` to DDR3
@`0x80000000` (1 MB) round-trips, `cmp` matches → **`DDR3 DMA OK`** at **~190 MB/s** (H2C 195, C2H 187 —
the Gen1 x1 line rate). The MIG calibrates and the SmartConnect revives. (Pi was innocent all along.)

> ⚠️ Reflash gotcha: `full.tcl` runs `reset_property BITSTREAM.CONFIG.SPI_BUSWIDTH` after `write_bitstream`
> (it's there for the DFX partial bit), so a GUI-regenerated `.bit` comes out **SPI x1** and `write_cfgmem
> -interface SPIx4` fails with `SPI_BUSWIDTH is set to "1"`. Fix in the GUI: **Tools → Edit Device
> Properties → Configuration**: SPI Buswidth **4**, Configuration Rate **50**, SPI Falling Edge **Yes**,
> Bitstream Compression **TRUE** → **Generate Bitstream** again → then make the `.mcs`.

## G. Boot automation — fully hands-off (no flasher)
Because the FPGA auto-configures from flash (step C), the whole runtime bring-up is software
and is automated by a **systemd oneshot** that runs D (rebind) + E (insmod + new_id):

`/usr/local/bin/unlook-fpga-up.sh`:
```bash
#!/bin/bash
PCIE=1000110000.pcie
XDMA_KO=/home/<user>/m2-artix7-accelerator-card/app/xdma-kernel-module/xdma/xdma.ko
echo "$PCIE" > /sys/bus/platform/drivers/brcm-pcie/unbind 2>/dev/null; sleep 1
echo "$PCIE" > /sys/bus/platform/drivers/brcm-pcie/bind;   sleep 1
for i in $(seq 1 10); do lspci -nn | grep -qi '10ee:7024' && break; sleep 0.5; done
lsmod | grep -q '^xdma ' || insmod "$XDMA_KO" poll_mode=1
echo "10ee 7024" > /sys/bus/pci/drivers/xdma/new_id 2>/dev/null
```
`/etc/systemd/system/unlook-fpga.service`: `Type=oneshot`, `ExecStart=/usr/local/bin/unlook-fpga-up.sh`,
`RemainAfterExit=yes`, `WantedBy=multi-user.target`. `systemctl enable --now unlook-fpga.service`.
Result: **cold power-on → device + `/dev/xdma0_*` ready, no manual steps, no JTAG.** (Rebuild `xdma.ko`
after a kernel update.)

---

## Known limitations → fixed on our own carrier
- **No auto-boot from flash** at power-on (needs JTAG `Boot from config memory` each time).
  On our custom carrier: strap **M[2:0]=001** (Master SPI), wire PROGRAM_B/INIT_B/DONE
  properly, keep **PERST# off PROGRAM_B**, and add clean power-on/config sequencing.
- Link is **Gen1 x1**; force Gen2 (`dtparam=pciex1_gen=2` + XDMA at Gen2) for more headroom.
- The XDMA driver patches above are local; for production set the IP device-id to a
  driver-recognized value and build the driver with `poll_mode` as default on the Pi.
