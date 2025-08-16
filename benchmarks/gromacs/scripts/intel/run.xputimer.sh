#!/bin/bash
TOOLS=$PROJ_ROOT/tools
SRC=$PROJ_ROOT/src
PIN_ROOT=$TOOLS/pin-3.30-98830-g1d7b601b3-gcc-linux
GTPIN_KIT=$TOOLS/gtpin-4.5.0-linux
source COMMAND.sh
echo $COMMAND
export ZET_ENABLE_API_TRACING_EXP=1
export ZET_ENABLE_PROGRAM_INSTRUMENTATION=1
export ZE_ENABLE_TRACING_LAYER=1

RESULTS_DIR=KOIPerf
HPCWL_COMMAND_PREFIX="$PIN_ROOT/pin  -t $SRC/XPU-Timer/Intel/CPUPinTool/obj-intel64/xpu-KOIPerf.so -outdir $RESULTS_DIR  -perfout cpu.onkernelperf.txt -probemode -gtpindir $GTPIN_KIT -gtpintool $SRC/XPU-Timer/Intel/GTPinTool/build/GTPinKOIPerf.so -gt --gpuoutdir -gt $RESULTS_DIR  -gt --gpu_perfout -gt gpu.onkernelperf.out -gt --perfOnKernel -- "
$HPCWL_COMMAND_PREFIX $COMMAND
