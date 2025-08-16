#!/usr/bin/env python3

import os
import argparse
import re
import logging
from pathlib import Path


def get_args():
  parser = argparse.ArgumentParser(description="Split GPU thread profiles")
  parser.add_argument("-n", "--nthreads", type=int, help="Number of threads")
  parser.add_argument("-g",
                      "--gpudir",
                      type=str,
                      default=".",
                      help="GPU profile directory")
  parser.add_argument("-o",
                      "--outdir",
                      type=str,
                      default="gpu-perthread",
                      help="Output directory created inside <gpudir>")
  parser.add_argument("-v",
                      "--verbose",
                      action='store_true',
                      help="Verbose output")
  args = parser.parse_args()
  return args


def get_num_threads(gpu_basedir):
  nthreads = 0
  threadfile = os.path.join(gpu_basedir, 'thread.bbv')

  if not os.path.isfile(threadfile):
    raise FileNotFoundError(f'{threadfile} not found.')

  with open(threadfile, 'r') as readf:
    for line in readf:
      if line.startswith('tid'):
        linekey = line.split(':')[0]
        mat = re.match(r"([a-z]+)([0-9]+)", linekey, re.I)
        if mat:
          items = mat.groups()
          thread_id = int(items[1])
          if thread_id > nthreads:
            nthreads = thread_id

  return nthreads + 1


def main(num_threads, gpu_basedir, out_basedir):
  logging.basicConfig(level=logging.INFO,
                      format='%(asctime)s - %(levelname)s - %(message)s')
  log = logging.getLogger(__name__)

  log.info(f'Using {num_threads} threads')

  if not Path(gpu_basedir).exists():
    raise FileNotFoundError(f"GPU directory not found: {gpu_basedir}")

  try:
    os.makedirs(out_basedir, exist_ok=True)
    log.info(f"Directory '{out_basedir}' created successfully")
  except OSError as error:
    raise OSError(f"Directory '{out_basedir}' cannot be created: {error}")

  f = []
  try:
    for i in range(num_threads):
      temp = open(os.path.join(out_basedir, f'T.{i}.bb'), 'w')
      f.append(temp)

    threadfile = os.path.join(gpu_basedir, 'thread.bbv')
    if not os.path.isfile(threadfile):
      raise FileNotFoundError(f"Thread file not found: {threadfile}")

    log.info("Processing thread profiles...")

    with open(threadfile, 'r') as readf:
      line_count = 0

      for line in readf:
        line_count += 1

        # Copy headers and markers to all files
        if line.startswith('#') or line.startswith('M:') or line.startswith(
            'S:'):
          for i in range(num_threads):
            f[i].write(line)

        # Process thread-specific lines
        elif line.startswith('tid'):
          thread = 0
          linekey = line.split(':')[0]
          mat = re.match(r"([a-z]+)([0-9]+)", linekey, re.I)

          if mat:
            items = mat.groups()
            thread = int(items[1])

            if thread >= num_threads:
              log.warning(
                  f"Thread ID {thread} exceeds expected threads {num_threads}")
              continue

          line_content = line.split(' ', 1)[-1]
          f[thread].write(line_content)

        if line_count % 100000 == 0:
          log.info(f"Processed {line_count} lines...")

    log.info(f"Successfully processed {line_count} lines")

  finally:
    for file_handle in f:
      if file_handle and not file_handle.closed:
        file_handle.close()

  log.info("Thread splitting completed successfully")


def split_threads(nthreads=None,
                  gpudir=".",
                  outdir="gpu-perthread",
                  verbose=False):
  if verbose:
    logging.getLogger().setLevel(logging.DEBUG)

  gpu_basedir = gpudir

  if nthreads is None:
    num_threads = get_num_threads(gpu_basedir)
  else:
    num_threads = nthreads

  out_basedir = os.path.join(gpu_basedir, outdir)
  main(num_threads, gpu_basedir, out_basedir)
  return out_basedir


if __name__ == '__main__':
  args = get_args()

  if args.verbose:
    logging.getLogger().setLevel(logging.DEBUG)

  try:
    gpu_basedir = args.gpudir

    if args.nthreads:
      num_threads = args.nthreads
    else:
      num_threads = get_num_threads(gpu_basedir)

    out_basedir = os.path.join(gpu_basedir, args.outdir)

    main(num_threads, gpu_basedir, out_basedir)

  except Exception as e:
    print(f"Error: {e}")
    exit(1)
