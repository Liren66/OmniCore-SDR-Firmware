# Packages OC_SYNC_AXI into component.xml next to hdl/.
# vivado -mode batch -source scripts/package_ip.tcl
# Only this IP directory and its build/ directory are written.
set ip_dir [file normalize [file join [file dirname [info script]] ..]]
set build_dir [file join $ip_dir build package]
file mkdir $build_dir
create_project -force oc_sync_package $build_dir -part xc7z020clg400-2
set_property target_language Verilog [current_project]
add_files [list [file join $ip_dir hdl ocs_hold_xfer.v] [file join $ip_dir hdl oc_sync_core.v] \
                [file join $ip_dir hdl oc_sync_axi.v]]
set_property top oc_sync_axi [current_fileset]
update_compile_order -fileset sources_1
ipx::package_project -root_dir $ip_dir -vendor user.org -library user -taxonomy /UserIP -set_current true
set core [ipx::current_core]
set_property name OC_SYNC_AXI $core
set_property version 1.0 $core
set_property display_name {OmniCore multi-board sync} $core
set_property description {REF_OUT 10 MHz from the 40 MHz VCTCXO, TRIG generator (command / periodic / at PPS), AD9361_SYNC and PPS_EXT edge counters, PPS selection for the time base.} $core
set_property vendor_display_name {OmniCore} $core
set_property supported_families {zynq Production} $core
set_property core_revision 1 $core

set clock_if [ipx::get_bus_interfaces S_AXI_CLK -of_objects $core]
set freq [ipx::get_bus_parameters FREQ_HZ -of_objects $clock_if]
if {[llength $freq] == 0} { set freq [ipx::add_bus_parameter FREQ_HZ $clock_if] }
set_property value 100000000 $freq
set_property value_resolve_type dependent $freq
set_property value_dependency {spirit:decode(id('PARAM_VALUE.CLK_FREQ_HZ'))} $freq
set user_freq [ipx::get_user_parameters CLK_FREQ_HZ -of_objects $core]
set_property display_name {AXI clock frequency (Hz)} $user_freq
set_property description {Must equal the actual s_axi_aclk frequency; used for the 1 s REF40_HZ gate.} $user_freq

set axi_if [ipx::get_bus_interfaces S_AXI -of_objects $core]
set map [ipx::get_memory_maps S_AXI -of_objects $core]
if {[llength $map] == 0} { set map [ipx::add_memory_map S_AXI $core] }
set_property slave_memory_map_ref S_AXI $axi_if
set block [ipx::get_address_blocks reg0 -of_objects $map]
if {[llength $block] == 0} { set block [ipx::add_address_block reg0 $map] }
set_property base_address 0 $block
set_property range 4096 $block
set_property width 32 $block
set_property usage register $block
foreach {name offset access description} {
    ID 0 read-only {0x4F435359 (OCSY)}
    VERSION 4 read-only {0x00010000}
    CTRL 8 read-write {0 ref_out_en 1 trig_en 2 trig_falling 3 periodic_en 4 pps_sel_sync 5 loopback}
    TRIG_WIDTH 12 read-write {TRIG high time in 40 MHz cycles}
    TRIG_PERIOD 16 read-write {Periodic TRIG period in 40 MHz cycles, minimum 1000}
    CMD 20 write-only {0 fire 1 arm_at_pps 2 disarm 3 clear_counters}
    STATUS 24 read-only {0 cfg_busy 1 ref40_present 2 armed 3 trig 4 sync 5 pps}
    REF40_HZ 28 read-only {Measured 40 MHz clock frequency}
    SYNC_COUNT 32 read-only {AD9361_SYNC rising edges}
    SYNC_INTERVAL 36 read-only {40 MHz cycles between the last two SYNC edges}
    PPS_COUNT 40 read-only {PPS_EXT rising edges}
    PPS_INTERVAL 44 read-only {40 MHz cycles between the last two PPS edges}
    TRIG_COUNT 48 read-only {TRIG pulses emitted}
} {
    set reg [ipx::get_registers $name -of_objects $block]
    if {[llength $reg] == 0} { set reg [ipx::add_register $name $block] }
    set_property address_offset $offset $reg
    set_property size 32 $reg
    set_property access $access $reg
    set_property description $description $reg
}
ipx::create_xgui_files $core
ipx::update_checksums $core
ipx::check_integrity $core
ipx::save_core $core
puts "OC_SYNC_PACKAGE_OK: [file join $ip_dir component.xml]"
close_project
