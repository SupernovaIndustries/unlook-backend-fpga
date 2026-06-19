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

## C. Configure the FPGA — ⚠️ this card does NOT auto-boot from flash
Confirmed: at power-on the FPGA does **not** auto-configure from flash (board-level
power-on / PERST sequencing). JTAG-commanded config works (blue LED = configured).
Until fixed in HW, after every power-on:
- Hardware Manager → right-click `xc7a200t_0` → **Boot from Configuration Memory Device**
  (reloads from flash via PROGRAM_B; blue LED on), **or** `Program Device` with the `.bit`.

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
   ls -l /dev/xdma*     # expect: xdma0_user, xdma0_h2c_0, xdma0_c2d_0, xdma0_control ...
   ```

## F. Verify the DMA datapath (DDR3)
With `/dev/xdma0_*` present, use the repo tools (`app/xdma-tools`) to write/read DDR3:
```bash
# write 1 MB of random data to DDR3 @ 0x80000000, read it back, compare
dd if=/dev/urandom of=/tmp/tx.bin bs=1M count=1
./dma_to_device   -d /dev/xdma0_h2c_0 -a 0x80000000 -s 0x100000 -f /tmp/tx.bin
./dma_from_device -d /dev/xdma0_c2d_0 -a 0x80000000 -s 0x100000 -f /tmp/rx.bin
cmp /tmp/tx.bin /tmp/rx.bin && echo "DDR3 DMA OK"
```
AXI-Lite registers (our core's ap_ctrl, once the SGM IP is integrated) are reached by
mmap-ing `/dev/xdma0_user` at the AXI-Lite offset.

## G. Boot automation (current workaround)
Until the card auto-boots from flash, a boot-time helper must run **after** the FPGA is
configured (step C, still JTAG-assisted): a systemd unit doing D (rebind) + E (insmod +
new_id). The JTAG config step is the only piece not yet automatable on this card.

---

## Known limitations → fixed on our own carrier
- **No auto-boot from flash** at power-on (needs JTAG `Boot from config memory` each time).
  On our custom carrier: strap **M[2:0]=001** (Master SPI), wire PROGRAM_B/INIT_B/DONE
  properly, keep **PERST# off PROGRAM_B**, and add clean power-on/config sequencing.
- Link is **Gen1 x1**; force Gen2 (`dtparam=pciex1_gen=2` + XDMA at Gen2) for more headroom.
- The XDMA driver patches above are local; for production set the IP device-id to a
  driver-recognized value and build the driver with `poll_mode` as default on the Pi.
