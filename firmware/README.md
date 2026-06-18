# Firmware (released bitstreams)

Versioned, production bitstreams for the Unlook SGM-Census core. These are the
artifacts the host loads/expects at runtime; the host verifies the version via
the `VERSION` AXI-Lite register against `expected_bitstream_version`.

## Naming
```
sgm_census_a200t_v<N>.bin      # raw config for the W25Q128 SPI flash
sgm_census_a200t_v<N>.bit      # JTAG bitstream (bench programming)
```
`<N>` MUST equal `kCoreVersion` in [`../host/xdma/RegisterMap.hpp`](../host/xdma/RegisterMap.hpp)
and what the gateware drives on the `VERSION` register. Bump it on ANY
host-visible change (register map, disparity encoding, buffer layout).

## Deployment (host)
Place the bitstream where the runtime loads it (e.g. `/lib/firmware/unlook/`) and
set the SDK `expected_bitstream_version` accordingly. A mismatch makes the SDK
refuse the FPGA and fall back to CPU (audited as `FPGA_BITSTREAM_MISMATCH`).

## Storage
Bitstreams are large binaries — track them with **git-lfs** (`*.bin`, `*.bit`
under `firmware/` only). Build-tree bitstreams under `gateware/` are git-ignored.

## Provenance (defense)
For each release record: core version, git commit of `gateware/`, Vivado version,
and a SHA-256 of the artifact, so the deployed bitstream is traceable.
