#!/usr/bin/env python3
#BEGIN_LEGAL 
#BSD License 
#
#Copyright (c)2022 Intel Corporation. All rights reserved.
#
#Redistribution and use in source and binary forms, with or without modification, 
# are permitted provided that the following conditions are met:
#
#1. Redistributions of source code must retain the above copyright notice, 
#   this list of conditions and the following disclaimer.
#
#2. Redistributions in binary form must reproduce the above copyright notice, 
#   this list of conditions and the following disclaimer in the documentation 
#   and/or other materials provided with the distribution.
#
#3. Neither the name of the copyright holder nor the names of its contributors 
#   may be used to endorse or promote products derived from this software without 
#   specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND 
# ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED 
# WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
# DISCLAIMED.
# IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
# INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
# BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
# DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
# LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
# OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
# ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
#END_LEGAL
#
#
# Read in a file with the following format
# slicenumber OnRun/Complete TSC tscvalue
# e.g.
# 1 OnRun Kernel1 TSC 1343417934171039
# 1 OnComplete Kernel1 TSC 1343417940232647
# 2 OnRun Kernel1 TSC 1343...
# 2 OnComplete Kernel1 TSC 134...
 


import datetime
import glob
import math
import optparse
import os
import random
import re
import sys
import argparse

from msg import ensure_string
slicenumber=0
inithwicount = 0 ; finihwicount = 0; comphwicount = 0; lastcomphwicount = 0;
slice_hwicount = [];

def str2bool(v):
    if isinstance(v, bool):
        return v
    if v.lower() in ('yes', 'true', 't', 'y', '1'):
        return True
    elif v.lower() in ('no', 'false', 'f', 'n', '0'):
        return False
    else:
        raise argparse.ArgumentTypeError('Boolean value expected.')

def PrintAndExit(msg):
    """
    Prints an error message exit.

    """
    sys.stderr.write(msg)
    sys.stderr.write("\n")
    sys.exit(-1)

def PrintMsg(msg):
    """
    Prints an message 
    """
    sys.stdout.write(msg)
    sys.stdout.write("\n")
    sys.stdout.flush()

def PrintMsgNoCR(msg):
    """
    Prints an message 
    """
    sys.stdout.write(msg)
    sys.stdout.flush()

def OpenFile(fl, type_str):
    """
    Check to make sure a file exists and open it.

    @return file pointer
    """

    # import pdb;  pdb.set_trace()
    if not os.path.isfile(fl):
        PrintAndExit('File does not exist: %s' % fl)
    try:
        fp = open(fl, 'rb')
    except IOError:
        PrintAndExit('Could not open file: %s' % fl)

    return fp

def IsInt(s):
    """
    Is a string an integer number?

    @return boolean
    """
    try:
        int(s)
        return True
    except ValueError:
        return False

def IsFloat(s):
    """
    Is a string an floating point number?

    @return boolean
    """
    try:
        float(s)
        return True
    except ValueError:
        return False



def OpenSliceRDTSCFile(fv_file, type_str):
    """
    Open a frequency vector file and check to make sure it's valid.  A valid
    FV file must contain at least one line which starts with the string 'T:'.

    @return file pointer
    """

    # import pdb;  pdb.set_trace()
    fp = OpenFile(fv_file, type_str)
    line = ensure_string(fp.readline())
    if not line.startswith('GPU_Init'):
        PrintAndExit("Invalid " + type_str + fv_file)
    fp.seek(0, 0)

    return fp

def OpenSimpointFile(sp_file, type_str):
    """
    Open a simpoint file and check to make sure it's valid.  A valid
    simpoint file must start with an integer.

    @return file pointer
    """

    fp = OpenFile(sp_file, type_str)
    line = ensure_string(fp.readline())
    field = line.split()
    if not IsInt(field[0]):
        PrintAndExit("Invalid " + type_str + sp_file)
    fp.seek(0, 0)

    return fp



def OpenLabelFile(lbl_file, type_str):
    """
    Open a labels file and check to make sure it's valid.  A valid
    labels file must start with an integer.

    @return file pointer
    """

    fp = OpenFile(lbl_file, type_str)
    line = ensure_string(fp.readline())
    field = line.split()
    if not IsInt(field[0]):
        PrintAndExit("Invalid " + type_str + lbl_file)
    fp.seek(0, 0)

    return fp

def OpenWeightsFile(wt_file, type_str):
    """
    Open a weight file and check to make sure it's valid.  A valid
    wieght file must start with an floating point number.

    @return file pointer
    """

    fp = OpenFile(wt_file, type_str)
    line = ensure_string(fp.readline())
    field = line.split()
    if not IsFloat(field[0]):
        PrintAndExit("Invalid " + weight_str + wt_file)
    fp.seek(0, 0)

    return fp


def GetSliceHWicount(fp_hwicount, inslicenum):
    """
    Get HWIcount delta for one slice
    delta should be between current on-complete-hwicount and 
      last on-complete-hwicount
    first slice (slice 0) delta is between current on-complete-hwicount 
      and init-hwicount
    last slice (slice N-1) delta is between fini-hwicount and 
      last on-complete-hwicount

    @return delta HWicount for the current slice
    """
    if inslicenum >=0  and inslicenum < len(slice_hwicount):
      return slice_hwicount[inslicenum]
    return -1

def ReadSliceHWicount(fp_hwicount):
    """
    Read cpu hwicount file
GPU_Init : TSC 1540689857970411
hw_cpu_cycles:1134988259 hw_instructions:2706116754 
KOI_STOP: TSC 1540727986812317
hw_cpu_cycles:62364694566 hw_instructions:139820030247 
..
GPU_Fini : TSC 1541251542225627
hw_cpu_cycles:1093082035189 hw_instructions:463536757174 
    @return delta HWicount for the current slice
    """
    global slice_hwicount
    global inithwicount, finihwicount, comphwicount, lastcomphwicount;

    line = ensure_string(fp_hwicount.readline())
    if line == '': return -1

    slice_num = 0
    #import pdb;  pdb.set_trace()
    while True:
      if line == '': return -1
      tokens = line.split()
      if tokens[0] == "GPU_Init": 
        line = ensure_string(fp_hwicount.readline())
        tokens = line.split()
        tokens2 = tokens[1].split(":")
        if tokens2[0] != "hw_instructions": return -1
        inithwicount = int(tokens2[1])
      elif  tokens[0] == "GPU_Fini": 
        line = ensure_string(fp_hwicount.readline())
        tokens = line.split()
        tokens2 = tokens[1].split(":")
        if tokens2[0] != "hw_instructions": return -1
        finihwicount = int(tokens2[1])
        # Last record, does not have any complete/run, use last comprdtsc
        slice_hwicount.insert(slice_num, (finihwicount - lastcomphwicount ));
        #print (slice_num,",",slice_hwicount[slice_num]);
        slice_num = slice_num + 1
      elif tokens[0] == "KOI_STOP:": 
        line = ensure_string(fp_hwicount.readline())
        tokens = line.split()
        tokens2 = tokens[1].split(":")
        if tokens2[0] != "hw_instructions": return -1
        comphwicount = int(tokens2[1])
        tmphwicount=lastcomphwicount
        lastcomphwicount=comphwicount
        if slice_num == 0: slice_hwicount.insert(slice_num, (comphwicount - inithwicount));
        else: slice_hwicount.insert(slice_num, (comphwicount - tmphwicount));
        #print (slice_num,",",slice_hwicount[slice_num]);
        slice_num = slice_num + 1
      line = ensure_string(fp_hwicount.readline())
    return 0

def ProcessLabelFile(fp_lbl):
    """
    Process records in a t.labels file
    cluster distance_from_centroid
    slice number is implicit here : sliceNumber = (lineNumber-1)
    @return an array mapping 'sliceN' to the cluster it belongs to
    """

    sliceCluster = [] 
    sliceNum=0
    line = ensure_string(fp_lbl.readline())
    while line:
        tokens = line.split(' ')
        clusterid = int(tokens[0])
        sliceCluster.append(clusterid)
        line = ensure_string(fp_lbl.readline())
        sliceNum = sliceNum + 1

    # import pdb;  pdb.set_trace()
    return sliceCluster

############################################################################
#
#  Functions for generating regions CSV files
#
############################################################################


def GetWeights(fp):
    """
    Get the regions and weights from a weights file.

    @return lists of regions and weights
    """

    # This defines the pattern used to parse one line of the weights file.
    # There are three components to each line:
    #
    #   1) a floating point number  (group number 1 in the match object)
    #   2) white space
    #   3) a decimal number         (group number 2 in the match object)
    #
    # 1) This matches floating point numbers in either fixed point or scientific notation:
    #   -?        optionally matches a negative sign (zero or one negative signs)
    #   \ *       matches any number of spaces (to allow for formatting variations like - 2.3 or -2.3)
    #   [0-9]+    matches one or more digits
    #   \.?       optionally matches a period (zero or one periods)
    #   [0-9]*    matches any number of digits, including zero
    #   (?: ... ) groups an expression, but without forming a "capturing group" (look it up)
    #   [Ee]      matches either "e" or "E"
    #   \ *       matches any number of spaces (to allow for formats like 2.3E5 or 2.3E 5)
    #   -?        optionally matches a negative sign
    #   \ *       matches any number of spaces
    #   [0-9]+    matches one or more digits
    #   ?         makes the entire non-capturing group optional (to allow for
    #             the presence or absence of the exponent - 3000 or 3E3
    #
    # 2) This matches white space:
    #   \s
    #
    # 3) This matches a decimal number with one, or more digits:
    #   \d+
    #
    pattern = '(-?\ *[0-9]+\.?[0-9]*(?:[Ee]\ *-?\ *[0-9]+)?)\s(\d+)'

    weight_dict = {}
    for line in ensure_string(fp.readlines()):
        field = re.match(pattern, line)

        # Look for the special case where the first field is a single digit
        # without the decimal char '.'.  This should be the weight of '1'.
        #
        if not field:
            field = re.match('(\d)\s(\d)', line)
        if field:
            weight = float(field.group(1))
            cluster = int(field.group(2))
            weight_dict[cluster] = weight

    return weight_dict


def GetSimpoints(fp):
    """
    Get the regions and slices from the Simpoint file.

    @return list of regions and slices from a Simpoint file
    """
    regionno=0
    SimpointToRegion = {}
    SimpointToCluster = {}
    for line in ensure_string(fp.readlines()):
        field = re.match('(\d+)\s(\d+)', line)
        if field:
            slice_num = int(field.group(1))
            cluster = int(field.group(2))
            regionno = regionno+1
            SimpointToRegion[slice_num] = regionno
            SimpointToCluster[slice_num] = cluster
    return SimpointToCluster, SimpointToRegion

def GenSummary(fp_hwicount, sliceCluster, SimpointToCluster, SimpointToRegion, weight_dict):
  global initrdtsc, finirdtsc
  slicenum = 0;
  print("Slice, HWicount, ClusterNumber, RegionNumber, Weight")
  #import pdb;  pdb.set_trace()
  while True:
    delta = GetSliceHWicount(fp_hwicount, slicenum)
    if delta == -1: break
    if slicenum in SimpointToCluster.keys() :
      simpoint_cluster = SimpointToCluster[slicenum]
      print (slicenum,",",delta,",",simpoint_cluster,",",SimpointToRegion[slicenum],",",weight_dict[simpoint_cluster])
    else:
      if 0 <= slicenum < len(sliceCluster):
        slice_cluster = sliceCluster[slicenum]
        print (slicenum,",",delta,",",slice_cluster)
      else:
        print (slicenum,"NOT in sliceCluster[]")
        break
    slicenum = slicenum+1;
  print("WholeProgram,", finihwicount-inithwicount);
  return

############################################################################
#
#  Functions for normalization and projection
#
############################################################################


def cleanup():
    """
    Close all open files and any other cleanup required.

    @return no return value
    """

    if fp_hwicount:
        fp_hwicount.close()
    if fp_lbl:
        fp_lbl.close()
    if fp_simp:
        fp_simp.close()

############################################################################

# report.slice-rdtsc.py --hwicount_file gpu.onkernelperf.out --region_file t.simpoints --label_file t.labels --weight_file t.weights > $prefix.llvmpoints.csv
parser = argparse.ArgumentParser()
parser.add_argument("--hwicount_file", help="HWIcount trace file", required=True)
parser.add_argument("--region_file", help="files showing simpoint regions", required=True)
parser.add_argument("--label_file", help="files showing per slice clusters", required=True)
parser.add_argument("--weights_file", help="files showing weights for simpoint regions", required=True)
args = parser.parse_args()
fp_lbl = OpenLabelFile(args.label_file, 'Slice label file: ')
sliceCluster = ProcessLabelFile(fp_lbl)
fp_hwicount = OpenSliceRDTSCFile(args.hwicount_file, 'CPU XPU-Perf trace file: ')
fp_simp = OpenSimpointFile(args.region_file, 'simpoints file: ')
fp_weight = OpenWeightsFile(args.weights_file, 'weights file: ')
weight_dict = GetWeights(fp_weight)
SimpointToCluster, SimpointToRegion = GetSimpoints(fp_simp)
#import pdb;  pdb.set_trace()
ReadSliceHWicount(fp_hwicount);
GenSummary(fp_hwicount, sliceCluster, SimpointToCluster, SimpointToRegion, weight_dict)
cleanup()
sys.exit(0)
