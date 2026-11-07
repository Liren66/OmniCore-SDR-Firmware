#!/usr/bin/env bash
# Runs the CH32H417_IP 1.3 time/framing testbench with Vivado 2018.3 xsim.
# Usage: run_sim.sh [work_dir]   (default: ./xsim_work next to this script)
set -euo pipefail
IP="$(cd "$(dirname "$0")/.." && pwd)"
WORK="${1:-$IP/sim/xsim_work}"
VIVADO="${XILINX_VIVADO:-/c/app/Xilinx/Vivado/2018.3}"
mkdir -p "$WORK"
cd "$WORK"
F="$IP/src"
xvlog --nolog \
    "$F/ch417_std_adc_dual/simulation/fifo_generator_vlog_beh.v" \
    "$F/ch417_std_adc_dual/hdl/fifo_generator_v13_2_rfs.v" \
    "$F/ch417_std_adc_single/sim/ch417_std_adc_single.v" \
    "$F/ch417_std_adc_dual/sim/ch417_std_adc_dual.v" \
    "$F/ch417_std_dac_single_1/sim/ch417_std_dac_single.v" \
    "$F/ch417_std_dac_dual/sim/ch417_std_dac_dual.v" \
    "$F/oc_afifo.v" "$F/oc_hold_xfer.v" "$F/oc_timekeeper.v" "$F/CH32H417_Fsm.v" \
    "$IP/hdl/CH32H417_IP_v1_0_S00_AXI.v" "$IP/hdl/CH32H417_IP_v1_0.v" \
    "$VIVADO/data/verilog/src/glbl.v"
xvlog --nolog -sv "$IP/sim/tb_ch32h417_time.sv"
xelab --nolog -L unisims_ver -L xpm --timescale 1ns/1ps -s tb_snap tb glbl
xsim --nolog tb_snap -R
