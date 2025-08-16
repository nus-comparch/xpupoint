#!/usr/bin/env python3
# BEGIN_LEGAL
# The MIT License (MIT)
#
# Copyright (c) 2024, National University of Singapore
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
import os, sys, getopt, glob


def usage(rc=0):
  print('''Usage:
  \t %s
  \t -h | --help
  \t -s | --sim-dir=<path/to/sim-results>
  \t -c | --csv=<path/to/xpuregions.csv>''' % sys.argv[0])
  exit(rc)


def read_pb_csv(data):
  csv_file = ''
  if data.endswith('xpuregions.csv'):
    csv_file = data
  else:
    csv_file = glob.glob(os.path.join(data, 'xpuregions.csv'))[0]

  region_multiplier = {}
  slice_multiplier = {}
  with open(csv_file) as _f:
    for line in _f:
      if not line:
        continue
      if line.startswith('cluster'):
        line = line.strip()
        fields = line.split(",")
        sim_slice = int(fields[0].split()[-1])
        sim_region = int(fields[2])
        region_multiplier[sim_region] = float(fields[10])
        slice_multiplier[sim_slice] = float(fields[10])
  return slice_multiplier


def get_all_region_ticks(sim_dir):
  kernel_len_ticks = []
  kernel_ticks = []
  region_end_ticks = []
  exit_tick = 0
  gpu_res_file = glob.glob(os.path.join(sim_dir + '/gpu_stats.txt'))[0]
  num_kernels = 0
  read_flag = 0
  with open(gpu_res_file) as _f:
    for line in _f:
      if 'start' in line and 'end' in line and 'exit' in line:
        if 'shader' in line:
          read_flag = 0
          continue
        else:
          read_flag = 1
          continue
      if read_flag:
        ticks = line.split(', ')
        kernel_len_ticks = [(int(ticks[i + 1]) - int(ticks[i]))
                            for i in range(0,
                                           len(ticks) - 1, 2)]
        exit_tick = int(ticks[len(ticks) - 1])
        kernel_ticks = [(int(ticks[i]), int(ticks[i + 1]))
                        for i in range(0,
                                       len(ticks) - 1, 2)]
        read_flag = 0

  end_ticks = [kernel_ticks[i][1] for i in range(0, len(kernel_ticks))]
  region_end_ticks.extend([0])
  region_end_ticks.extend(end_ticks)
  region_end_ticks.extend([exit_tick])
  region_len_ticks = [(int(region_end_ticks[i + 1]) - int(region_end_ticks[i]))
                      for i in range(0,
                                     len(region_end_ticks) - 1)]
  print(region_len_ticks)
  print(len(region_len_ticks))
  return region_len_ticks


def get_final_tick(sim_dir):
  res_file = glob.glob(os.path.join(sim_dir + '/stats.txt'))[0]
  with open(res_file) as _f:
    for line in _f:
      if 'final_tick' in line:
        return int(line.split()[1])
  return 0


def extrapolate(scaling_factor, region_ticks):
  proj_ticks = 0
  for _r in scaling_factor.keys():
    proj_ticks += scaling_factor[_r] * region_ticks[_r]
  return proj_ticks


if __name__ == "__main__":
  data_dir = ''
  sim_dir = ''

  try:
    opts, args = getopt.getopt(sys.argv[1:], 'hc:s:',
                               ['help', 'csv=', 'sim-dir='])
  except getopt.GetoptError as e:
    print(e)
    usage(1)
  for o, a in opts:
    if o == '-h' or o == '--help':
      usage(0)
    if o == '-c' or o == '--csv':
      csv_f = a
    if o == '-s' or o == '--sim-dir':
      sim_dir = a

  if not (os.path.exists(sim_dir) and os.listdir(sim_dir)):
    print('Error: Some directories do not exist or are empty.')
    sys.exit(1)
  sim_dir = os.path.abspath(sim_dir)
  csv_f = os.path.abspath(csv_f)
  scaling_factor = read_pb_csv(csv_f)
  sliceids = list(scaling_factor.keys())
  region_ticks = get_all_region_ticks(sim_dir)
  proj_ticks = extrapolate(scaling_factor, region_ticks)
  final_tick = get_final_tick(sim_dir)
  pred_err = round(((final_tick - proj_ticks) / final_tick) * 100, 2)
  par_speedup = round(final_tick / max(region_ticks), 2)
  print(max(region_ticks))
  print('wp_ticks,region_ticks,err%')
  print('%s,%s,%s' % (final_tick, proj_ticks, pred_err))
