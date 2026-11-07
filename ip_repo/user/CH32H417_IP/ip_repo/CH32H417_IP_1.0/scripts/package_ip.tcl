# Repackages CH32H417_IP as version 1.3 (framed transport only, time base).
# vivado -mode batch -source package_ip.tcl -tclargs <scratch_dir>
set ip_dir [file normalize [file join [file dirname [info script]] ..]]
set work [file normalize [lindex $argv 0]]
file mkdir $work
create_project -force ch32_ip_host $work/host -part xc7z020clg400-2
ipx::edit_ip_in_project -upgrade true -name ch32_ip_edit -directory $work/edit \
    $ip_dir/component.xml
set core [ipx::current_core]

foreach f {oc_afifo.v oc_hold_xfer.v oc_timekeeper.v} {
    if {[llength [get_files -quiet $ip_dir/src/$f]] == 0} {
        add_files -norecurse $ip_dir/src/$f
    }
}
update_compile_order -fileset sources_1
ipx::merge_project_changes files $core
ipx::merge_project_changes hdl_parameters $core
ipx::merge_project_changes ports $core

# Plain data ports: no inferred interfaces for the PPS and reference inputs.
foreach p {pps_ext ref_clk_in} {
    foreach bi [ipx::get_bus_interfaces -quiet -of_objects $core] {
        foreach pm [ipx::get_port_maps -quiet -of_objects $bi] {
            if {[get_property physical_name $pm] eq $p} {
                puts "removing inferred interface [get_property name $bi] for $p"
                ipx::remove_bus_interface [get_property name $bi] $core
            }
        }
    }
}
# Expose REF_HZ, keep the AXI address width at 7 bits (32 registers).
set up [ipx::get_user_parameters C_S00_AXI_ADDR_WIDTH -of_objects $core]
set_property value 7 $up
set_property value 7 [ipx::get_hdl_parameters C_S00_AXI_ADDR_WIDTH -of_objects $core]
set rp [ipx::get_user_parameters -quiet REF_HZ -of_objects $core]
if {$rp eq ""} { set rp [ipx::add_user_parameter REF_HZ $core] }
set_property value 40000000 $rp
set_property value_format long $rp
set_property display_name {AD9361 CLK_OUT reference frequency (Hz)} $rp
set_property value 32 [ipx::get_bus_parameters WIZ_NUM_REG \
    -of_objects [ipx::get_bus_interfaces S00_AXI -of_objects $core]]

set_property version 1.3 $core
set_property display_name CH32H417_IP_v1.3 $core
set_property description {CH32H417 UHSIF bridge: framed 16 KiB transport with 64-bit sample time base, PPS and timed RX/TX} $core
set_property core_revision [expr {[get_property core_revision $core] + 1}] $core
ipx::create_xgui_files $core
ipx::update_checksums $core
set errs [ipx::check_integrity $core]
puts "integrity: $errs"
ipx::save_core $core
close_project
puts "PACKAGE_OK"
