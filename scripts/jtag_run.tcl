# Loads bitstream + PS init + ELF over JTAG into RAM (no flash write).
# xsct scripts/jtag_run.tcl <system_wrapper.bit> <ps7_init.tcl> <app.elf>
set bit  [lindex $argv 0]
set init [lindex $argv 1]
set elf  [lindex $argv 2]
connect
targets -set -nocase -filter {name =~ "APU*"}
rst -system
after 1500
targets -set -nocase -filter {name =~ "xc7z020*"}
fpga -file $bit
targets -set -nocase -filter {name =~ "APU*"}
source $init
ps7_init
ps7_post_config
targets -set -nocase -filter {name =~ "*Cortex-A9 MPCore #0*"}
rst -processor
dow $elf
con
after 500
puts "JTAG_RUN_OK"
