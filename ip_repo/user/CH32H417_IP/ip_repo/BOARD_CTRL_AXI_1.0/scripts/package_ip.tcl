# Run: vivado -mode batch -source scripts/package_ip.tcl
# Only this IP directory and its build/ directory are written.
set ip_dir [file normalize [file join [file dirname [info script]] ..]]
set build_dir [file join $ip_dir build package]
file mkdir $build_dir
create_project -force board_ctrl_package $build_dir -part xc7z020clg400-2
set_property target_language Verilog [current_project]
add_files [glob [file join $ip_dir hdl *.v]]
set_property top board_ctrl_axi [current_fileset]
update_compile_order -fileset sources_1
ipx::package_project -root_dir $ip_dir -vendor user.org -library user -taxonomy /UserIP -set_current true
set core [ipx::current_core]
set_property name BOARD_CTRL_AXI $core
set_property version 1.0 $core
set_property display_name {Board RGB and Fan AXI-Lite} $core
set_property description {Single WS2812C-2020 RGB LED and counter-based fan PWM. Four registers, one AXI clock, no interrupts or DMA.} $core
set_property vendor_display_name {Local board peripherals} $core
set_property supported_families {zynq Production} $core
set_property core_revision 1 $core

set clock_if [ipx::get_bus_interfaces S_AXI_CLK -of_objects $core]
set freq [ipx::add_bus_parameter FREQ_HZ $clock_if]
set_property value 100000000 $freq
set_property value_resolve_type dependent $freq
set_property value_dependency {spirit:decode(id('PARAM_VALUE.CLK_FREQ_HZ'))} $freq
set user_freq [ipx::get_user_parameters CLK_FREQ_HZ -of_objects $core]
set_property display_name {AXI clock frequency (Hz)} $user_freq
set_property description {Must equal the actual s_axi_aclk frequency. Supported: 50 to 200 MHz. Default: 100 MHz.} $user_freq
set_property value_validation_type range_long $user_freq
set_property value_validation_range_minimum 50000000 $user_freq
set_property value_validation_range_maximum 200000000 $user_freq

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
    RGB 0 read-write {Color 0x00RRGGBB; frame snapshots prevent tearing.}
    FAN_PERIOD 4 read-write {PWM period in clock cycles; zero is treated as one.}
    FAN_HIGH 8 read-write {PWM high cycles; zero is low and values >= period are high.}
    CLOCK_HZ 12 read-only {Configured AXI clock frequency in Hz.}
} {
    set reg [ipx::add_register $name $block]
    set_property address_offset $offset $reg
    set_property size 32 $reg
    set_property access $access $reg
    set_property description $description $reg
}
ipx::create_xgui_files $core
ipx::update_checksums $core
ipx::check_integrity $core
ipx::save_core $core
puts "BOARD_CTRL_PACKAGE_OK: [file join $ip_dir component.xml]"
close_project
