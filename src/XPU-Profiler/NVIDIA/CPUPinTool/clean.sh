#!/bin/bash
# Copyright (C) 2022 Intel Corporation
# SPDX-License-Identifier: BSD-3-Clause
set -x
make clean
rm -r -f pin.log obj-*
make -C ../NVBitTool clean
