#!/usr/bin/env python3

import os
import argparse
import subprocess
import logging
from pathlib import Path


def get_args():
  parser = argparse.ArgumentParser(description="Run SimPoint clustering")
  parser.add_argument("-m",
                      "--maxk",
                      type=int,
                      default=20,
                      help="maxK for Kmeans clustering (20)")
  parser.add_argument("-d",
                      "--dim",
                      type=int,
                      default=15,
                      help="Number of reduced dimensions (15)")
  parser.add_argument("-b",
                      "--bbvfile",
                      type=str,
                      default="./T.global.hv",
                      help="The BBV file to sample (T.global.hv)")
  parser.add_argument("-o", "--outdir", type=str, help="Output directory")
  parser.add_argument("--no-regions",
                      default=False,
                      action='store_true',
                      help="No xpuregions.csv file will be generated")
  parser.add_argument("--fixed-length",
                      type=str,
                      default="on",
                      help="Passed to SimPoint -fixedLength (on)/off")
  parser.add_argument("--gpu-only",
                      default=False,
                      action='store_true',
                      help="Generate GPU regions instead of XPU regions")
  parser.add_argument("--simpoint-bin",
                      type=str,
                      default="",
                      help="Path to SimPoint binary")
  parser.add_argument("-v",
                      "--verbose",
                      action='store_true',
                      help="Enable verbose output")
  args = parser.parse_args()
  return args


def get_simpoint_from_env():
  sde_kit = os.environ.get('SDE_BUILD_KIT')
  if not sde_kit:
    raise EnvironmentError("SDE_BUILD_KIT environment variable not set")

  simpoint = os.path.join(sde_kit,
                          'pinplay-scripts/PinPointsHome/Linux/bin/simpoint')
  if not os.path.isfile(simpoint):
    raise FileNotFoundError(f'SimPoint binary not found: {simpoint}')

  return simpoint


def run_simpoint(simpoint_bin, globalbbv, maxk, dim, outdir, fixed_length):
  tsimpoints = os.path.join(outdir, 't.simpoints')
  tweights = os.path.join(outdir, 't.weights')
  tlabels = os.path.join(outdir, 't.labels')

  if not os.path.isfile(globalbbv):
    raise FileNotFoundError(f'BBV file not found: {globalbbv}')

  if fixed_length != "off":
    fixed_length = "on"

  cmd = [
      simpoint_bin, '-loadFVFile', globalbbv, '-maxK',
      str(maxk), '-dim',
      str(dim), '-coveragePct', '1.0', '-saveSimpoints', tsimpoints,
      '-saveSimpointWeights', tweights, '-saveLabels', tlabels, '-fixedLength',
      fixed_length, '-verbose', '1'
  ]

  logging.info('Running SimPoint clustering...')
  logging.debug(f'Command: {" ".join(cmd)}')

  try:
    result = subprocess.run(cmd, check=True, capture_output=True, text=True)
    if result.stdout:
      logging.debug(f'SimPoint stdout: {result.stdout}')
    if result.stderr:
      logging.warning(f'SimPoint stderr: {result.stderr}')
  except subprocess.CalledProcessError as e:
    raise RuntimeError(
        f'SimPoint failed with exit code {e.returncode}: {e.stderr}')

  for f in [tsimpoints, tweights, tlabels]:
    if not os.path.isfile(f):
      raise RuntimeError(f'SimPoint failed to create: {f}')

  logging.info('SimPoint clustering completed successfully')
  return tsimpoints, tweights, tlabels


def gen_regions(globalbbv, tsimpoints, tweights, tlabels, outdir, gpu_only):
  utils_dir = os.path.dirname(os.path.abspath(__file__))
  xpu_regions_script = os.path.join(utils_dir, 'xpu_regions.py')

  if not os.path.isfile(xpu_regions_script):
    raise FileNotFoundError(f'xpu_regions.py not found: {xpu_regions_script}')

  if gpu_only:
    regions_csv = os.path.join(outdir, 'gpuregions.csv')
  else:
    regions_csv = os.path.join(outdir, 'xpuregions.csv')

  logging.info(f'Generating {os.path.basename(regions_csv)}')

  cmd = [
      'python3', xpu_regions_script, f'--bbv_file={globalbbv}',
      f'--region_file={tsimpoints}', f'--weight_file={tweights}',
      f'--label_file={tlabels}', '--csv_region'
  ]

  logging.debug(f'Command: {" ".join(cmd)}')

  try:
    with open(regions_csv, 'w') as outfile:
      result = subprocess.run(cmd,
                              check=True,
                              stdout=outfile,
                              stderr=subprocess.PIPE,
                              text=True)
      if result.stderr:
        logging.warning(f'xpu_regions.py stderr: {result.stderr}')
  except subprocess.CalledProcessError as e:
    raise RuntimeError(
        f'Region generation failed with exit code {e.returncode}: {e.stderr}')

  if not os.path.isfile(regions_csv) or os.path.getsize(regions_csv) == 0:
    raise RuntimeError(f'Failed to generate regions file: {regions_csv}')

  logging.info(f'Region file generated: {regions_csv}')
  return regions_csv


def main(maxk,
         dim,
         globalbbv,
         outdir,
         no_regions=False,
         gpu_only=False,
         fixed_length="on",
         simpoint_bin=""):
  logging.basicConfig(level=logging.INFO,
                      format='%(asctime)s - %(levelname)s - %(message)s')

  try:
    if not simpoint_bin:
      simpoint_bin = get_simpoint_from_env()

    Path(outdir).mkdir(parents=True, exist_ok=True)

    tsimpoints, tweights, tlabels = run_simpoint(simpoint_bin, globalbbv, maxk,
                                                 dim, outdir, fixed_length)

    if not no_regions:
      gen_regions(globalbbv, tsimpoints, tweights, tlabels, outdir, gpu_only)

    logging.info('SimPoint analysis completed successfully')

  except Exception as e:
    logging.error(f'SimPoint analysis failed: {e}')
    raise


def run_cluster(maxk=20,
                dim=15,
                bbvfile="./T.global.hv",
                outdir=None,
                no_regions=False,
                gpu_only=False,
                fixed_length="on",
                verbose=False):
  if verbose:
    logging.getLogger().setLevel(logging.DEBUG)

  if not outdir:
    outdir = os.path.dirname(os.path.abspath(bbvfile))

  return main(maxk, dim, bbvfile, outdir, no_regions, gpu_only, fixed_length)


if __name__ == '__main__':
  args = get_args()

  if args.verbose:
    logging.getLogger().setLevel(logging.DEBUG)

  try:
    maxk = args.maxk
    dim = args.dim
    globalbbv = args.bbvfile
    bbv_path = os.path.dirname(os.path.abspath(globalbbv))

    if args.outdir:
      outdir = args.outdir
    else:
      outdir = bbv_path

    main(maxk, dim, globalbbv, outdir, args.no_regions, args.gpu_only,
         args.fixed_length, args.simpoint_bin)

  except Exception as e:
    print(f"Error: {e}")
    exit(1)
