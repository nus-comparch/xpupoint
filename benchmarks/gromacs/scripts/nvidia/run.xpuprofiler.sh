#!/bin/bash
TOOLS=$PROJ_ROOT/tools
SRC=$PROJ_ROOT/src
PIN_ROOT=$TOOLS/pin-3.30-98830-g1d7b601b3-gcc-linux
NVBIT_KIT=$TOOLS/nvbit-Linux-x86_64-1.5.5
source COMMAND.sh
echo $COMMAND
ulimit -s unlimited
TOOLBASE=$SRC/XPU-Sampler/NVIDIA-GPU
RESULTS_DIR=BasicBlocks

LD_PRELOAD=$SRC/XPU-Profiler/NVIDIA/NVBitTool/NVBitTool.so $PIN_ROOT/pin -t $SRC/XPU-Profiler/NVIDIA/CPUPinTool/obj-intel64/xpu-pin-kernelsampler.so -bbprofile -bbfocusthread 0 -emit_vectors 0 -bbverbose 1 -nvbittool  $SRC/XPU-Profiler/NVIDIA/NVBitTool/NVBitTool.so -- $COMMAND

