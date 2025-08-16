#!/bin/bash
TOOLS=$PROJ_ROOT/tools
SRC=$PROJ_ROOT/src
PIN_ROOT=$TOOLS/pin-3.30-98830-g1d7b601b3-gcc-linux
NVBIT_KIT=$TOOLS/nvbit-Linux-x86_64-1.5.5
source COMMAND.sh
echo $COMMAND

RESULTS_DIR=KOIPerf
export TOOL_GPU_OUTDIR=$RESULTS_DIR
export TOOL_GPU_PERFOUT=gpu.onkernelperf.out
HPCWL_COMMAND_PREFIX="$PIN_ROOT/pin -t $SRC/XPU-Timer/NVIDIA/CPUPinTool/obj-intel64/xpu-pin-nvbit-handler.so -outdir $RESULTS_DIR -perfout cpu.onkernelperf.txt -probemode -nvbittool $SRC/XPU-Timer/NVIDIA/NVBitTool/NVBitTool.so -- "

LD_PRELOAD=$SRC/XPU-Timer/NVIDIA/NVBitTool/NVBitTool.so $HPCWL_COMMAND_PREFIX $COMMAND
