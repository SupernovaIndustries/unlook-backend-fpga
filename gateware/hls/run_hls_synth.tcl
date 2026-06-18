# run_hls_synth.tcl -- synthesis + IP export ONLY (no csim/cosim).
# Use on any machine with Vitis HLS but WITHOUT the reference vectors / SDK
# (e.g. the Windows build box). Verifies the core is synthesizable and reports
# resources/timing, then packages the IP. Full functional parity is run via
# run_hls.tcl on a host that has the CPU golden vectors.
#
#   vitis_hls -f run_hls_synth.tcl

set PART   xc7a200tfbg484-2   ;# TODO: set to the exact XC7A200T package on the card
set PERIOD 10                 ;# ns (100 MHz)

open_project sgm_hls_prj
set_top sgm_census
add_files sgm_census.cpp -cflags "-I."

open_solution "sol1"
set_part $PART
create_clock -period $PERIOD -name default

csynth_design

# NOTE: export_design in the Vivado 2021.1 BATCH IP packager can fail with
#   "'<bignum>' is an invalid argument. Please specify an integer value."
# This is a known tool bug: the auto core_revision is derived from the date/time
# (e.g. 2606182106 for 2026-06-18) and overflows int32 -- it is NOT a problem
# with the core (csynth above succeeds and the RTL is generated). Workarounds:
#   * export the IP from the Vitis HLS GUI (Solution > Export RTL), or
#   * use Vivado/Vitis HLS 2025.x.
# Once exported, gateware/vivado/create_project.tcl picks the IP up.
export_design -format ip_catalog -display_name "Unlook SGM-Census" -vendor supernova -library unlook -version 1.0

exit
