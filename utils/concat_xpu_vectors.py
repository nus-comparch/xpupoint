#!/usr/bin/env python3

# BEGIN_LEGAL
# The MIT License (MIT)
#
# Copyright (c) 2025, National University of Singapore
#
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in all
# copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
# SOFTWARE.
# END_LEGAL

import sys
import os
import collections
from functools import reduce
import numpy as np
import argparse
import logging
from pathlib import Path


class BBVConcat:

  def __init__(self, num_threads, cpu_basedir, gpu_basedir, out_basedir, mode):
    self.num_threads = num_threads
    self.cpu_basedir = cpu_basedir
    self.gpu_basedir = gpu_basedir
    self.out_basedir = out_basedir
    self.mode = mode
    self.max_bb = -1
    self.bb_pieces = {}
    self.marker_list = []

    logging.basicConfig(level=logging.INFO,
                        format='%(asctime)s - %(levelname)s - %(message)s')
    self.log = logging.getLogger(__name__)

    self._validate()

  def _validate(self):
    if self.mode not in ["cpu", "gpu", "xpu"]:
      raise ValueError(f"Invalid mode: {self.mode}")

    if self.mode in ["cpu", "xpu"] and not Path(self.cpu_basedir).exists():
      raise ValueError(f"CPU directory not found: {self.cpu_basedir}")

    if self.mode in ["gpu", "xpu"] and not Path(self.gpu_basedir).exists():
      raise ValueError(f"GPU directory not found: {self.gpu_basedir}")

    Path(self.out_basedir).mkdir(parents=True, exist_ok=True)

  def _get_bb_files(self):
    bb_files = []
    num_bb_files = 0
    default_bb_dir = self.cpu_basedir

    if self.mode == "cpu":
      num_bb_files = self.num_threads
    elif self.mode == "xpu":
      num_bb_files = self.num_threads - 1
    elif self.mode == "gpu":
      num_bb_files = self.num_threads
      default_bb_dir = self.gpu_basedir

    for f in range(num_bb_files):
      bb_path = Path(default_bb_dir) / f"T.{f}.bb"
      if not bb_path.exists():
        raise FileNotFoundError(f"Basic block file not found: {bb_path}")
      bb_files.append(open(bb_path, "r"))

    # Add GPU BBV for XPU mode
    if self.mode == "xpu":
      gpu_bbv = Path(self.gpu_basedir) / "global.bbv"
      if not gpu_bbv.exists():
        raise FileNotFoundError(f"GPU BBV file not found: {gpu_bbv}")
      bb_files.append(open(gpu_bbv, "r"))

    return bb_files

  def _find_max_bb(self, bb_files):
    self.log.info("Finding maximum basic block ID...")
    max_bb = int(-1)
    thread_line_count = [int(0)] * self.num_threads

    while True:
      end_of_file = [False] * self.num_threads

      for f in range(self.num_threads):
        if f >= len(bb_files):
          end_of_file[f] = True
          continue

        while True:
          line = bb_files[f].readline()
          if line == "":
            end_of_file[f] = True
            break

          if line and line.startswith('T:'):
            thread_line_count[f] += 1
            max_vals = map(lambda x: int(x.split(':')[1]),
                           filter(lambda x: x, line[1:].rstrip().split(' ')))
            max_bb_tmp = max(max_vals)
            max_bb = max(max_bb, max_bb_tmp)
            break

      if reduce(lambda x, y: x and y, end_of_file):
        break

    self.log.info(f'max_bb: {max_bb}')
    self.max_bb = max_bb
    return max_bb

  def _process_vectors(self, bb_files):
    self.log.info("Processing basic block vectors...")
    global_kernel_marker = ''
    global_kernel_count_marker = 0
    thread_line_count = [int(0)] * self.num_threads

    while True:
      end_of_file = [False] * self.num_threads

      for f in range(self.num_threads):
        if f >= len(bb_files):
          end_of_file[f] = True
          continue

        while True:
          line = bb_files[f].readline()
          if line == "":
            end_of_file[f] = True
            break

          if line and line.startswith('# Slice ending'):
            global_kernel_marker = (line.split()[-3])
            global_kernel_count_marker = int(line.split()[-1])

          elif line and line[0] == 'T':
            thread_line_count[f] += 1

            if (global_kernel_marker,
                global_kernel_count_marker) not in self.bb_pieces:
              self.bb_pieces[global_kernel_marker,
                             global_kernel_count_marker] = {}

            self.bb_pieces[
                global_kernel_marker, global_kernel_count_marker][f] = np.array(
                    list(
                        map(
                            lambda x: ':%s:%s' % x,
                            map(
                                lambda x:
                                (int(x.split(':')[1]) +
                                 (self.max_bb * f), int(x.split(':')[2])),
                                filter(lambda x: x,
                                       line[1:].rstrip().split(' '))))))
            break

      if reduce(lambda x, y: x and y, end_of_file):
        break

  def _get_markers(self):
    self.log.info('Using Thread 0 BBV for event ordering.')

    global_fn = '%s/T.0.bb' % (self.cpu_basedir)
    if self.mode == "gpu":
      global_fn = '%s/T.0.bb' % (self.gpu_basedir)

    try:
      with open(global_fn, 'r') as global_in:
        for line in global_in:
          if line and line.startswith('# Slice ending at '):
            global_kernel_marker = (line.split()[-3])
            global_kernel_count_marker = int(line.split()[-1])
            self.marker_list.append(
                (global_kernel_marker, global_kernel_count_marker))
    except IOError as e:
      err_str = 'Unable to open the global BBV file: [%s]' % global_fn
      self.log.error(err_str)
      raise

    self.log.info(f"Found {len(self.marker_list)} markers")

  def _write_output(self):
    self.log.info("Writing output file...")

    out = []
    if self.mode == "xpu":
      out.append(open("%s/T.global.hv" % (self.out_basedir,), "w"))
    elif self.mode == "cpu":
      out.append(open("%s/T.global.cv" % (self.out_basedir,), "w"))
    elif self.mode == "gpu":
      out.append(open("%s/global.bbv" % (self.out_basedir,), "w"))

    log = open("%s/concat-vectors.log" % (self.out_basedir,), "w")

    ordered_bb_pieces = collections.OrderedDict(sorted(self.bb_pieces.items()))

    for i, k in enumerate(self.marker_list):
      print(k)
      if i > 0:
        out[0].write('M: %s %s\n' %
                     (self.marker_list[i - 1][0], self.marker_list[i - 1][1]))
      else:
        out[0].write('M: SYS_init 1\n')

      out[0].write('# Slice ending at kernel %s count %s\n' % (k[0], str(k[1])))
      out[0].write('T')

      if k in ordered_bb_pieces:
        ordered_thread_bb_pieces = collections.OrderedDict(
            sorted(ordered_bb_pieces[k].items()))
        thread_ins_contri = [0.0] * (self.num_threads)
        tot_ins = 0

        for k1, v1 in ordered_thread_bb_pieces.items():
          th_ins = sum([int(el.split(':')[-1]) for el in v1])
          tot_ins += th_ins
          thread_ins_contri[k1] = th_ins

          if len(ordered_thread_bb_pieces[k1]) == 0:
            continue

          out[0].write(' '.join(ordered_thread_bb_pieces[k1]))
          out[0].write(' ')

        if tot_ins == 0:
          err_str = 'Found slice without instructions, icounts: %s' % str(k)
          print(err_str)
          log.write(err_str)

      out[0].write('\n')

    if self.mode == "xpu":
      out[0].write('M: SYS_exit 1')

    out[0].close()
    log.close()

    self.log.info("Output written successfully")

  def run(self):
    try:
      bb_files = self._get_bb_files()
      self._find_max_bb(bb_files)
      for f in bb_files:
        f.seek(0)

      self._process_vectors(bb_files)

      for f in bb_files:
        f.close()

      self._get_markers()
      self._write_output()
      self.log.info("Vector concatenation completed successfully")

    except Exception as e:
      self.log.error(f"Error during concatenation: {e}")
      raise


def get_args():
  parser = argparse.ArgumentParser()
  parser.add_argument("-n",
                      "--cputhreads",
                      type=int,
                      default=8,
                      help="Number of CPU threads")
  parser.add_argument("-w",
                      "--gputhreads",
                      type=int,
                      default=32,
                      help="Number of GPU threads")
  parser.add_argument("-c", "--cpudir", type=str, help="CPU profile directory")
  parser.add_argument("-g", "--gpudir", type=str, help="GPU profile directory")
  parser.add_argument("-o", "--outdir", type=str, help="Output directory")
  args = parser.parse_args()
  return args


def main(num_threads, cpu_basedir, gpu_basedir, out_basedir, mode):
  bbv_concat = BBVConcat(num_threads, cpu_basedir, gpu_basedir, out_basedir,
                         mode)
  bbv_concat.run()


if __name__ == '__main__':
  args = get_args()
  mode = ""
  num_threads = 0
  num_cpu_threads = args.cputhreads
  num_gpu_threads = args.gputhreads

  if args.cpudir:
    cpu_basedir = args.cpudir
  else:
    cpu_basedir = ""

  if args.gpudir:
    gpu_basedir = args.gpudir
  else:
    gpu_basedir = ""

  if args.outdir:
    out_basedir = args.outdir
  elif cpu_basedir:
    out_basedir = cpu_basedir
  elif gpu_basedir:
    out_basedir = gpu_basedir

  if cpu_basedir and gpu_basedir:
    num_threads = num_cpu_threads + 1
    mode = "xpu"
  elif cpu_basedir and not gpu_basedir:
    num_threads = num_cpu_threads
    mode = "cpu"
  elif gpu_basedir and not cpu_basedir:
    num_threads = num_gpu_threads
    mode = "gpu"
  else:
    print("Require either CPU or GPU profile directories to continue.")
    exit(1)

  main(num_threads, cpu_basedir, gpu_basedir, out_basedir, mode)
