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
initrdtsc = 0; finirdtsc = 0; runrdtsc = 0; comprdtsc = 0; lastcomprdtsc = 0;
sum_run_rdtsc=0;
gpu_only = False;

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

    #import pdb;  pdb.set_trace()
    fp = OpenFile(fv_file, type_str)
    line = ensure_string(fp.readline())
    if not line.startswith('0 GPU_Init'):
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


def GetSliceRDTSC(fp_rdtsc, inslicenum):
    """
    Get RDTSC delta for one slice
    delta should be between current on-complete-rdtsc and 
      last on-complete-rdrsc
    first slice (slice 0) delta is between current on-complete-rdtsc 
      and init-rdtsc
    last slice (slice N-1) delta is between fini-rdtsc and 
      last on-complete-rdtsc

    @return delta RDTSC for the current slice
    """
    global initrdtsc, finirdtsc, runrdtsc, comprdtsc, lastcomprdtsc, sum_run_rdtsc

    line = ensure_string(fp_rdtsc.readline())
    if line == '': return -1

    #import pdb;  pdb.set_trace()
    while True:
      tokens = line.split()
      # print 'Slice:'
      slice_num = int(tokens[0])
      if not slice_num == inslicenum:
        break
      if tokens[1] == "GPU_Init": initrdtsc=int(tokens[4])
      elif  tokens[1] == "GPU_Fini": 
        finirdtsc=int(tokens[4])
        # Last record, does not have any complete/run, use last comprdtsc
        return (finirdtsc - comprdtsc )
      elif tokens[1] == "OnRun": 
        runrdtsc=int(tokens[4])
      elif tokens[1] == "OnComplete": 
        comprdtsc=int(tokens[4])
        if gpu_only:
          if (runrdtsc == 0): PrintAndExit('OnComplete without OnRun')
          retval = (comprdtsc - runrdtsc)
          sum_run_rdtsc += retval
          runrdtsc=0
          return retval
        else:
          tmprdtsc=lastcomprdtsc
          lastcomprdtsc=comprdtsc
          if slice_num == 0: return (comprdtsc - initrdtsc)
          else: return (comprdtsc - tmprdtsc)
      line = ensure_string(fp_rdtsc.readline())
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

def GenSummary(fp_rdtsc, sliceCluster, SimpointToCluster, SimpointToRegion, weight_dict):
  global initrdtsc, finirdtsc
  slicenum = 0;
  print("Slice, RDTSC, ClusterNumber, RegionNumber, Weight")
  #import pdb;  pdb.set_trace()
  while True:
    delta = GetSliceRDTSC(fp_rdtsc, slicenum)
    if delta == -1: break
    if slicenum in SimpointToCluster.keys() :
      simpoint_cluster = SimpointToCluster[slicenum]
      print (slicenum,",",delta,",",simpoint_cluster,",",SimpointToRegion[slicenum],",",weight_dict[simpoint_cluster])
    else:
      if 0 <= slicenum < len(sliceCluster):
        slice_cluster = sliceCluster[slicenum]
        print (slicenum,",",delta,",",slice_cluster)
      #else:
        #print (slicenum,"NOT in sliceCluster[]")
        #break
    slicenum = slicenum+1;
  if gpu_only:
    print("WholeProgram,", sum_run_rdtsc);
  else:
    print("WholeProgram,", finirdtsc-initrdtsc);
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

    if fp_rdtsc:
        fp_rdtsc.close()
    if fp_lbl:
        fp_lbl.close()
    if fp_simp:
        fp_simp.close()

############################################################################

# report.slice-rdtsc.py --rdtsc_file gpu.onkernelperf.out --region_file t.simpoints --label_file t.labels --weight_file t.weights > $prefix.llvmpoints.csv
parser = argparse.ArgumentParser()
parser.add_argument("--rdtsc_file", help="RDTSV trace file", required=True)
parser.add_argument("--region_file", help="files showing simpoint regions", required=True)
parser.add_argument("--label_file", help="files showing per slice clusters", required=True)
parser.add_argument("--weights_file", help="files showing weights for simpoint regions", required=True)
#parser.add_argument("--gpu_only", help="Get kernel rdtsc values (OnComplete - OnRun)", action=argparse.BooleanOptionalAction)
parser.add_argument("--gpu_only", type=str2bool, nargs='?',
                        const=True, default=False,
                        help="Get kernel rdtsc values (OnComplete - OnRun)")
args = parser.parse_args()
fp_lbl = OpenLabelFile(args.label_file, 'Slice label file: ')
sliceCluster = ProcessLabelFile(fp_lbl)
fp_rdtsc = OpenSliceRDTSCFile(args.rdtsc_file, 'RDTSC trace file: ')
fp_simp = OpenSimpointFile(args.region_file, 'simpoints file: ')
fp_weight = OpenWeightsFile(args.weights_file, 'weights file: ')
weight_dict = GetWeights(fp_weight)
gpu_only = args.gpu_only
SimpointToCluster, SimpointToRegion = GetSimpoints(fp_simp)
#import pdb;  pdb.set_trace()
GenSummary(fp_rdtsc, sliceCluster, SimpointToCluster, SimpointToRegion, weight_dict)
cleanup()
sys.exit(0)
