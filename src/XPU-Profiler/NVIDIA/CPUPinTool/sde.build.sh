#!/bin/bash
# Copyright (C) 2022 Intel Corporation
# SPDX-License-Identifier: BSD-3-Clause
if [[  -z $SDE_BUILD_KIT ]]; then
    echo "SDE_BUILD_KIT not defined"
    exit 1
fi

if [ ! -e $SDE_BUILD_KIT/pinplay-scripts ];
then
  echo "$SDE_BUILD_KIT/pinplay-scripts does not exist"
  echo "git clone https://github.com/intel/pinplay-tools.git"
  echo "cp -r pinplay-tools/pinplay-scripts $SDE_BUILD_KIT/"
  exit 1
fi

if [ -z $CUDA_LIB ];
then
  echo "Put CUDA compiler/runtime in your environment: put the location on 'nvcc' in your PATH and set CUDA_LIB, CUDA_INC"
  exit 1
fi
if [ -z $NVBIT_KIT ];
then
  echo "Set NVBIT_KIT to point to the latest NVBit kit from https://github.com/NVlabs/NVBit/releases"
  exit 1
fi
if [[  -z $MBUILD ]]; then
    echo "MBUILD not defined"
    echo " mbuild clone https://github.com/intelxed/mbuild.git"
    echo " set MBUILD to point to 'mbuild' just cloned"
    exit 1
fi

rm -rf obj-intel64
export PYTHONPATH=$SDE_BUILD_KIT/pinplay-scripts:$MBUILD
./mfile.py --host-cpu x86-64 
cd ../NVBitTool
make clean; make
