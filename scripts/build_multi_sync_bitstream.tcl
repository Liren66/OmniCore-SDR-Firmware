# Adds OC_SYNC_AXI (multi-board sync) to system.bd, builds the bitstream and
# exports system_wrapper.bit + system_wrapper.hdf. Idempotent.
# vivado -mode batch -source scripts/build_multi_sync_bitstream.tcl -tclargs <out_dir> [jobs]
set root [file normalize [file join [file dirname [info script]] ..]]
set out [file normalize [lindex $argv 0]]
set jobs [expr {[llength $argv] > 1 ? [lindex $argv 1] : 8}]
file mkdir $out

open_project $root/OmniCore-SDR-Firmware.xpr
update_ip_catalog -rebuild
set bd [get_files system.bd]
open_bd_design $bd

proc port_if_missing {name dir {type ""}} {
    if {[llength [get_bd_ports -quiet $name]] == 0} {
        if {$type eq ""} { create_bd_port -dir $dir $name } else { create_bd_port -dir $dir -type $type $name }
    }
    return [get_bd_ports $name]
}
set clk_fb [port_if_missing CLK_FB_FPGA I clk]
set_property CONFIG.FREQ_HZ 40000000 $clk_fb
port_if_missing REF_OUT O
port_if_missing TRIG O
port_if_missing AD9361_SYNC I

set cell [get_bd_cells -quiet OC_SYNC_AXI_0]
if {[llength $cell] == 0} {
    set cell [create_bd_cell -type ip -vlnv user.org:user:OC_SYNC_AXI:1.0 OC_SYNC_AXI_0]
} else {
    upgrade_ip [get_ips -quiet system_OC_SYNC_AXI_0_0]
}
puts "OC_SYNC VLNV: [get_property VLNV [get_bd_cells OC_SYNC_AXI_0]]"

proc connect_if_free {pin args} {
    if {[llength [get_bd_nets -quiet -of_objects [get_bd_pins $pin]]] == 0} {
        connect_bd_net {*}$args [get_bd_pins $pin]
    }
}
# AXI: the unused master M01 of the CPU interconnect (already clocked).
if {[llength [get_bd_intf_nets -quiet -of_objects [get_bd_intf_pins OC_SYNC_AXI_0/S_AXI]]] == 0} {
    connect_bd_intf_net [get_bd_intf_pins axi_cpu_interconnect/M01_AXI] [get_bd_intf_pins OC_SYNC_AXI_0/S_AXI]
}
connect_if_free OC_SYNC_AXI_0/s_axi_aclk [get_bd_pins sys_ps7/FCLK_CLK0]
connect_if_free OC_SYNC_AXI_0/s_axi_aresetn [get_bd_pins sys_rstgen/peripheral_aresetn]
connect_if_free OC_SYNC_AXI_0/clk_40m [get_bd_ports CLK_FB_FPGA]
connect_if_free OC_SYNC_AXI_0/sync_in [get_bd_ports AD9361_SYNC]
connect_if_free OC_SYNC_AXI_0/ref_out [get_bd_ports REF_OUT]
connect_if_free OC_SYNC_AXI_0/trig_out [get_bd_ports TRIG]
# PPS_EXT now reaches the time base through the sync IP's PPS selection.
set pps_net [get_bd_nets -quiet -of_objects [get_bd_pins CH32H417_IP_0/pps_ext]]
if {[llength $pps_net] && [llength [get_bd_pins -quiet -of_objects $pps_net -filter {NAME == pps_out}]] == 0} {
    disconnect_bd_net $pps_net [get_bd_pins CH32H417_IP_0/pps_ext]
}
connect_if_free OC_SYNC_AXI_0/pps_ext_in [get_bd_ports gps_pps_0]
connect_if_free CH32H417_IP_0/pps_ext [get_bd_pins OC_SYNC_AXI_0/pps_out]

set seg [get_bd_addr_segs -quiet -of_objects [get_bd_addr_spaces sys_ps7/Data] -filter {NAME =~ *OC_SYNC*}]
if {[llength $seg] == 0} {
    assign_bd_address -offset 0x43C30000 -range 64K [get_bd_addr_segs OC_SYNC_AXI_0/S_AXI/reg0]
}
validate_bd_design
save_bd_design
make_wrapper -files $bd -top -force
generate_target all $bd

reset_run synth_1
launch_runs impl_1 -to_step write_bitstream -jobs $jobs
wait_on_run impl_1
puts "impl status: [get_property STATUS [get_runs impl_1]]"
open_run impl_1
report_timing_summary -max_paths 20 -file $out/timing_summary.rpt
report_utilization -file $out/utilization.rpt
report_cdc -file $out/cdc.rpt
report_drc -file $out/drc.rpt
puts "WNS=[get_property STATS.WNS [get_runs impl_1]] WHS=[get_property STATS.WHS [get_runs impl_1]]"
set bit [glob [get_property DIRECTORY [get_runs impl_1]]/*.bit]
file copy -force $bit $out/system_wrapper.bit
write_hwdef -force -file $out/system_wrapper.hwdef
write_sysdef -force -hwdef $out/system_wrapper.hwdef -bitfile $out/system_wrapper.bit \
    -file $out/system_wrapper.hdf
puts "MULTI_SYNC_BUILD_DONE"
