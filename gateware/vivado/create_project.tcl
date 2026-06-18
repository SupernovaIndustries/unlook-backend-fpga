# create_project.tcl -- integrate the HLS sgm_census IP into the rigoorozco
# xdma_ddr3 reference design and build a bitstream + .mcs.
#
#   vivado -mode batch -source create_project.tcl
#
# Assumes:
#   * Vivado 2021.2.
#   * The rigoorozco `xdma_ddr3` example block design is available (XDMA PCIe
#     endpoint + MIG DDR3). Set XDMA_DDR3_BD_TCL to its block-design tcl.
#   * The HLS IP has been exported (gateware/hls/run_hls.tcl) to
#     gateware/hls/sgm_hls_prj/sol1/impl/ip.
#
# The SGM core attaches with TWO connections to that BD (confirm the cell names
# against your example -- they are the only environment-specific lines):
#   1. sgm_census/m_axi_gmem  -> the DDR3 MIG user AXI (via the existing
#      AXI SmartConnect/interconnect that the XDMA also uses for DDR3).
#   2. sgm_census/s_axi_control <- the XDMA AXI-Lite master (M_AXI_LITE / M_AXI_B),
#      so the host reaches ap_ctrl + the gmem base register over the _user BAR.
# Clock/reset: drive the core from the same user clock as the XDMA AXI domain.

set PART        xc7a200tfbg484-2
set PROJ_NAME   unlook_sgm
set PROJ_DIR    [file normalize [file join [pwd] build_vivado]]
set REPO_ROOT   [file normalize [file join [pwd] .. ..]]
set HLS_IP_DIR  [file join $REPO_ROOT gateware hls sgm_hls_prj sol1 impl ip]

# Path to the reference xdma_ddr3 block-design tcl (EDIT to your checkout).
set XDMA_DDR3_BD_TCL "$::env(HOME)/m2-artix7-accelerator-card/projects/xdma_ddr3/xdma_ddr3_bd.tcl"

create_project $PROJ_NAME $PROJ_DIR -part $PART -force

# HLS IP into the catalog.
set_property ip_repo_paths $HLS_IP_DIR [current_project]
update_ip_catalog

# Recreate the reference XDMA+DDR3 block design.
if {![file exists $XDMA_DDR3_BD_TCL]} {
    puts "ERROR: set XDMA_DDR3_BD_TCL to the rigoorozco xdma_ddr3 block-design tcl."
    exit 1
}
source $XDMA_DDR3_BD_TCL
set bd [current_bd_design]

# Add the SGM core.
set sgm [create_bd_cell -type ip -vlnv supernova:unlook:sgm_census:1.0 sgm_census_0]

# --- environment-specific wiring (confirm cell/pin names vs your BD) ---------
# Reuse the XDMA user clock + reset for the core's AXI domain:
#   connect_bd_net [get_bd_pins <xdma>/axi_aclk]    [get_bd_pins sgm_census_0/ap_clk]
#   connect_bd_net [get_bd_pins <xdma>/axi_aresetn] [get_bd_pins sgm_census_0/ap_rst_n]
# Route s_axi_control from the XDMA AXI-Lite master, and m_axi_gmem to the MIG
# AXI (through the existing SmartConnect), then:
#   assign_bd_address    ;# map sgm_census_0/s_axi_control into the XDMA _user BAR,
#                         ;# and m_axi_gmem onto the MIG DDR3 range at kDdr3Base.
# -----------------------------------------------------------------------------

regenerate_bd_layout
validate_bd_design
save_bd_design

# Top wrapper + build.
make_wrapper -files [get_files ${bd}.bd] -top
add_files -norecurse [file join $PROJ_DIR ${PROJ_NAME}.gen sources_1 bd $bd hdl ${bd}_wrapper.v]
import_files -fileset constrs_1 -norecurse [file join $REPO_ROOT gateware constraints sgm.xdc]

launch_runs synth_1 -jobs 8
wait_on_run synth_1
launch_runs impl_1 -to_step write_bitstream -jobs 8
wait_on_run impl_1

# Configuration memory image for the Winbond W25Q128 SPI flash.
set BIT  [file join $PROJ_DIR ${PROJ_NAME}.runs impl_1 ${bd}_wrapper.bit]
set MCS  [file join $REPO_ROOT firmware sgm_census_a200t_v1.mcs]
write_cfgmem -format mcs -interface SPIx4 -size 16 \
    -loadbit "up 0x0 $BIT" -file $MCS -force
puts "Bitstream: $BIT"
puts "Flash image: $MCS"
exit
