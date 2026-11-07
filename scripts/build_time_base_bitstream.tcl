# Upgrades CH32H417_IP to 1.3 in system.bd, wires PPS / AD9361 CLK_OUT,
# builds the bitstream and exports system_wrapper.bit + system_wrapper.hdf.
# vivado -mode batch -source scripts/build_time_base_bitstream.tcl -tclargs <out_dir> [jobs]
set root [file normalize [file join [file dirname [info script]] ..]]
set out [file normalize [lindex $argv 0]]
set jobs [expr {[llength $argv] > 1 ? [lindex $argv 1] : 8}]
file mkdir $out

open_project $root/OmniCore-SDR-Firmware.xpr
update_ip_catalog -rebuild
set bd [get_files system.bd]
open_bd_design $bd

set cell [get_bd_cells CH32H417_IP_0]
# Upgrade also after a revision bump of the same 1.3 version.
report_ip_status -name ip_status
upgrade_ip [get_ips system_CH32H417_IP_0_0]
set cell [get_bd_cells CH32H417_IP_0]
puts "CH32 VLNV: [get_property VLNV $cell]"
if {[get_property CONFIG.C_S00_AXI_ADDR_WIDTH $cell] != 7} {
    set_property -dict [list CONFIG.C_S00_AXI_ADDR_WIDTH {7}] $cell
}
puts "CH32 AXI addr width: [get_property CONFIG.C_S00_AXI_ADDR_WIDTH $cell]"

# External PPS (J6 PPS_EXT -> Y7) also feeds the time base.
if {[llength [get_bd_nets -quiet -of_objects [get_bd_pins CH32H417_IP_0/pps_ext]]] == 0} {
    connect_bd_net [get_bd_ports gps_pps_0] [get_bd_pins CH32H417_IP_0/pps_ext]
}
# AD9361 CLK_OUT (U10) -> internal 1 PPS generator.
if {[llength [get_bd_ports -quiet AD9361_CLK_OUT]] == 0} {
    create_bd_port -dir I -type clk AD9361_CLK_OUT
    set_property CONFIG.FREQ_HZ 40000000 [get_bd_ports AD9361_CLK_OUT]
}
if {[llength [get_bd_nets -quiet -of_objects [get_bd_pins CH32H417_IP_0/ref_clk_in]]] == 0} {
    connect_bd_net [get_bd_ports AD9361_CLK_OUT] [get_bd_pins CH32H417_IP_0/ref_clk_in]
}
validate_bd_design
save_bd_design
make_wrapper -files $bd -top -force
set wrapper [glob -nocomplain [file dirname $bd]/hdl/system_wrapper.v]
puts "wrapper: $wrapper"
generate_target all $bd

reset_run synth_1
launch_runs impl_1 -to_step write_bitstream -jobs $jobs
wait_on_run impl_1
set st [get_property STATUS [get_runs impl_1]]
puts "impl status: $st"
open_run impl_1
report_timing_summary -max_paths 20 -file $out/timing_summary.rpt
report_utilization -file $out/utilization.rpt
report_cdc -file $out/cdc.rpt
set wns [get_property STATS.WNS [get_runs impl_1]]
set whs [get_property STATS.WHS [get_runs impl_1]]
puts "WNS=$wns WHS=$whs"
set bit [glob [get_property DIRECTORY [get_runs impl_1]]/*.bit]
file copy -force $bit $out/system_wrapper.bit
write_hwdef -force -file $out/system_wrapper.hwdef
write_sysdef -force -hwdef $out/system_wrapper.hwdef -bitfile $out/system_wrapper.bit \
    -file $out/system_wrapper.hdf
puts "BUILD_DONE"
