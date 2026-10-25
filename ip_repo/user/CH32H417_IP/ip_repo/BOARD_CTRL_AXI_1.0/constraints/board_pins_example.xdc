# EXAMPLE ONLY: not included in packaged IP constraints.
# Rename external BD ports to PL_RGB and PL_FAN_CONTRL before using this snippet.
# AD9361_MIX(3).pdf page 1: RGB=E19, FAN=D19; Bank35=3.3 V on page 3.
set_property PACKAGE_PIN E19 [get_ports PL_RGB]
set_property IOSTANDARD LVCMOS33 [get_ports PL_RGB]
set_property SLEW SLOW [get_ports PL_RGB]
set_property DRIVE 8 [get_ports PL_RGB]

set_property PACKAGE_PIN D19 [get_ports PL_FAN_CONTRL]
set_property IOSTANDARD LVCMOS33 [get_ports PL_FAN_CONTRL]
set_property SLEW SLOW [get_ports PL_FAN_CONTRL]
set_property DRIVE 8 [get_ports PL_FAN_CONTRL]
