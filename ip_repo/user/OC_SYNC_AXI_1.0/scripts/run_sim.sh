#!/usr/bin/env bash
# Runs the OC_SYNC_AXI testbench with Vivado 2018.3 xsim.
# Usage: run_sim.sh [work_dir]   (default: ./build/sim next to this IP)
set -euo pipefail
IP="$(cd "$(dirname "$0")/.." && pwd)"
WORK="${1:-$IP/build/sim}"
VIVADO="${XILINX_VIVADO:-/c/app/Xilinx/Vivado/2018.3}"
mkdir -p "$WORK"
cd "$WORK"
xvlog --nolog "$IP/hdl/ocs_hold_xfer.v" "$IP/hdl/oc_sync_core.v" "$IP/hdl/oc_sync_axi.v" \
    "$VIVADO/data/verilog/src/glbl.v"
xvlog --nolog -sv "$IP/tb/tb_oc_sync_axi.sv"
xelab --nolog -L unisims_ver --timescale 1ns/1ps -s tb_snap tb glbl
xsim --nolog tb_snap -R
