# sgm.xdc -- constraints for the SGM-Census core integration.
#
# The PCIe pinout/lane-swap and the DDR3/MIG pin constraints come from the
# rigoorozco xdma_ddr3 reference design (do NOT duplicate them here). The core
# is clocked by the XDMA user AXI clock, so no new primary clock is created.
#
# This file holds only SGM-core-specific exceptions. Add timing-closure
# constraints here as the design is tuned on hardware, e.g.:
#
#   # If the core ends up on a slower clock than the AXI domain, declare a CDC:
#   # set_clock_groups -asynchronous -group [get_clocks axi_clk] -group [get_clocks sgm_clk]
#
# Configuration: SPI x4 fast boot from the W25Q128 (matches write_cfgmem in
# create_project.tcl). Uncomment if not already set by the reference design:
# set_property BITSTREAM.CONFIG.SPI_BUSWIDTH 4 [current_design]
# set_property BITSTREAM.CONFIG.CONFIGRATE 33 [current_design]
# set_property CONFIG_MODE SPIx4 [current_design]
