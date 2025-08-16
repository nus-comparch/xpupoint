#!/usr/bin/env python3

import os
import argparse
import logging
from pathlib import Path


def get_args():
  parser = argparse.ArgumentParser(
      description="Generate instruction weights for clusters")
  parser.add_argument(
      "-d",
      "--data-dir",
      type=str,
      help=
      "SimPoint output directory (require T.global.hv, t.simpoints, t.labels)")
  parser.add_argument("-g",
                      "--global-bbv",
                      type=str,
                      default="T.global.hv",
                      help="The global BBV file used for clustering")
  parser.add_argument("-v",
                      "--verbose",
                      action='store_true',
                      help="Enable verbose output")
  args = parser.parse_args()
  return args


def read_slice_counts(globalbbv):
  allrcounts = []

  if not os.path.isfile(globalbbv):
    raise FileNotFoundError(f"Global BBV file not found: {globalbbv}")

  logging.info("Reading slice instruction counts...")

  with open(globalbbv, 'r') as f:
    for line in f:
      if line.startswith('T:'):
        rcount = 0
        linesplit = line.split()
        for el in linesplit:
          if ':' in el:
            rcount += int(el.split(':')[-1])
        allrcounts.append(rcount)

  if not allrcounts:
    raise ValueError("No instruction counts found in BBV file")

  logging.info(f"Found {len(allrcounts)} slices")
  return allrcounts


def calc_slice_weights(allrcounts):
  sumtot = sum(allrcounts)

  if sumtot == 0:
    raise ValueError("Total instruction count is zero")

  slice_weights = {}
  for i, element in enumerate(allrcounts):
    slice_weights[i] = round(element / sumtot, 9)

  logging.info(f"Total instructions: {sumtot}")
  return slice_weights, sumtot


def read_region_map(datadir):
  simpoints_file = os.path.join(datadir, 't.simpoints')

  if not os.path.isfile(simpoints_file):
    raise FileNotFoundError(f"SimPoints file not found: {simpoints_file}")

  region_slice_map = {}

  with open(simpoints_file, 'r') as f:
    for line in f:
      parts = line.split()
      if len(parts) >= 2:
        sliceid, regionid = parts[0], parts[1]
        region_slice_map[int(regionid)] = int(sliceid)

  if not region_slice_map:
    raise ValueError("No region mappings found in simpoints file")

  logging.info(f"Found {len(region_slice_map)} region mappings")
  return region_slice_map


def count_regions(datadir):
  labels_file = os.path.join(datadir, 't.labels')

  if not os.path.isfile(labels_file):
    raise FileNotFoundError(f"Labels file not found: {labels_file}")

  region_counts = {}

  with open(labels_file, 'r') as f:
    for line in f:
      parts = line.split()
      if len(parts) >= 1:
        regionid = int(parts[0])
        if regionid not in region_counts:
          region_counts[regionid] = 1
        else:
          region_counts[regionid] += 1

  if not region_counts:
    raise ValueError("No region counts found in labels file")

  logging.info(f"Found {len(region_counts)} unique regions")
  return region_counts


def calc_cluster_weights(slice_weights, region_slice_map, region_counts):
  cluster_weight = []
  weight_sum = 0

  for regionid in region_counts.keys():
    if regionid not in region_slice_map:
      logging.warning(f"Region {regionid} not found in slice map, skipping")
      continue

    sliceid = region_slice_map[regionid]
    if sliceid not in slice_weights:
      logging.warning(f"Slice {sliceid} not found in weights, skipping")
      continue

    curr_wt = round(slice_weights[sliceid] * region_counts[regionid], 8)
    cluster_weight.append((curr_wt, regionid))
    weight_sum += curr_wt

  # Sort by region ID for consistent output
  cluster_weight.sort(key=lambda tup: tup[1])

  logging.info(f"Generated {len(cluster_weight)} cluster weights")
  return cluster_weight, weight_sum


def write_weights(datadir, cluster_weight, weight_sum):
  weights_file = os.path.join(datadir, 't.iweights')

  logging.info(f"Writing weights to: {weights_file}")

  with open(weights_file, 'w') as fw:
    for wt, rid in cluster_weight:
      fw.write(f"{wt} {rid}\n")

  logging.info(f"Sum of cluster weights: {weight_sum}")

  # Validate weight sum (should be close to 1.0)
  if abs(weight_sum - 1.0) > 0.01:
    logging.warning(
        f"Weight sum ({weight_sum}) deviates significantly from 1.0")

  return weights_file


def main(datadir, globalbbv='T.global.hv'):
  logging.basicConfig(level=logging.INFO,
                      format='%(asctime)s - %(levelname)s - %(message)s')

  logging.info("Generating cluster weights based on slice lengths")

  try:
    if not os.path.isdir(datadir):
      raise FileNotFoundError(f"Data directory not found: {datadir}")

    if globalbbv == 'T.global.hv' or not os.path.isabs(globalbbv):
      globalbbv = os.path.join(datadir, globalbbv)

    allrcounts = read_slice_counts(globalbbv)
    slice_weights, sumtot = calc_slice_weights(allrcounts)
    region_slice_map = read_region_map(datadir)
    region_counts = count_regions(datadir)
    cluster_weight, weight_sum = calc_cluster_weights(slice_weights,
                                                      region_slice_map,
                                                      region_counts)
    weights_file = write_weights(datadir, cluster_weight, weight_sum)

    logging.info("Weight generation completed successfully")
    return weights_file

  except Exception as e:
    logging.error(f"Weight generation failed: {e}")
    raise


def gen_weights(datadir, globalbbv='T.global.hv', verbose=False):
  if verbose:
    logging.getLogger().setLevel(logging.DEBUG)

  return main(datadir, globalbbv)


if __name__ == '__main__':
  args = get_args()

  if args.verbose:
    logging.getLogger().setLevel(logging.DEBUG)

  try:
    datadir = args.data_dir
    globalbbv = args.global_bbv

    if not datadir:
      print("Error: --data-dir is required")
      exit(1)

    main(datadir, globalbbv)

  except Exception as e:
    print(f"Error: {e}")
    exit(1)
