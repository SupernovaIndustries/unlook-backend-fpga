# run_hls.tcl -- build/verify/export the sgm_census core with Vitis HLS.
#   vitis_hls -f run_hls.tcl
#
# Prereq for csim/cosim: generate the reference vectors next to this script:
#   tools/gen_golden  (writes params.bin left.bin right.bin disp_golden.bin)
# Then run from that directory (or copy the .bin files here).

set PART   xc7a200tfbg484-2   ;# TODO: set to the exact XC7A200T package on the card
set PERIOD 10                 ;# ns (100 MHz); tighten once timing is understood

open_project sgm_hls_prj
set_top sgm_census
add_files sgm_census.cpp -cflags "-I."
add_files -tb tb_sgm_census.cpp -cflags "-I."

open_solution "sol1"
set_part $PART
create_clock -period $PERIOD -name default

# Functional parity vs the CPU golden (the key check).
csim_design
# Synthesize to RTL + resource/timing estimate.
csynth_design
# RTL/C co-simulation (cycle-accurate parity).
cosim_design
# Package as an IP for the Vivado block design.
export_design -format ip_catalog -display_name "Unlook SGM-Census" -vendor supernova -library unlook -version 1.0

exit
