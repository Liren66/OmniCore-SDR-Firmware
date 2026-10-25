# Standalone catalog/BD validation and out-of-context synthesis; no main project edits.
set ip_dir [file normalize [file join [file dirname [info script]] ..]]
set build_dir [file join $ip_dir build check]
file mkdir $build_dir
create_project -force board_ctrl_check $build_dir -part xc7z020clg400-2
set_property ip_repo_paths [list $ip_dir] [current_project]
update_ip_catalog
create_bd_design board_ctrl_check_bd
set ctrl [create_bd_cell -type ip -vlnv user.org:user:BOARD_CTRL_AXI:1.0 ctrl]
set clk [create_bd_port -dir I -type clk clk]
set_property CONFIG.FREQ_HZ 100000000 $clk
set rst [create_bd_port -dir I -type rst resetn]
set_property CONFIG.POLARITY ACTIVE_LOW $rst
connect_bd_net $clk [get_bd_pins ctrl/s_axi_aclk]
connect_bd_net $rst [get_bd_pins ctrl/s_axi_aresetn]
make_bd_intf_pins_external [get_bd_intf_pins ctrl/S_AXI]
make_bd_pins_external [get_bd_pins ctrl/rgb_data] [get_bd_pins ctrl/fan_pwm]
assign_bd_address -offset 0x00000000 -range 4K
validate_bd_design
save_bd_design
puts "BOARD_CTRL_BD_OK"
# Exercise a nondefault parameter through the packaged IP, not only raw RTL.
set_property CONFIG.CLK_FREQ_HZ 125000000 $ctrl
set_property CONFIG.FREQ_HZ 125000000 $clk
validate_bd_design
save_bd_design
generate_target all [get_files */board_ctrl_check_bd.bd]
puts "BOARD_CTRL_CATALOG_GENERATE_OK_125MHZ"
close_project
if {[llength $argv] && [lindex $argv 0] eq "catalog"} { exit 0 }

create_project -in_memory -part xc7z020clg400-2
read_verilog [glob [file join $ip_dir hdl *.v]]
synth_design -top board_ctrl_axi -part xc7z020clg400-2 -mode out_of_context
create_clock -name axi_clk -period 10.000 [get_ports s_axi_aclk]
report_utilization -file [file join $build_dir utilization.rpt]
report_drc -file [file join $build_dir drc.rpt]
write_checkpoint -force [file join $build_dir board_ctrl_synth.dcp]
opt_design
place_design
route_design
report_timing_summary -file [file join $build_dir timing.rpt]
write_checkpoint -force [file join $build_dir board_ctrl_routed.dcp]
puts "BOARD_CTRL_SYNTH_ROUTE_OK"
close_project
