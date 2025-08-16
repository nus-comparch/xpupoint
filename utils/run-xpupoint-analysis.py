#!/usr/bin/env python3

import os
import sys
import argparse
import logging
from pathlib import Path
from typing import Optional

try:
  import threadsplit
  import concat_xpu_vectors
  import run_simpoint
  import gen_insweights
except ImportError as e:
  print(f"Error: Failed to import required module: {e}")
  sys.exit(1)


class Runner:
  
  def __init__(self, args):
    self.args = args
    self.setup_log()
    self.validate()
  
  def setup_log(self):
    lvl = logging.DEBUG if self.args.verbose else logging.INFO
    logging.basicConfig(
      level=lvl,
      format='%(asctime)s - %(levelname)s - %(message)s'
    )
    self.log = logging.getLogger(__name__)
  
  def validate(self):
    if not Path(self.args.cpudir).exists():
      raise ValueError(f"CPU directory does not exist: {self.args.cpudir}")
    
    if not Path(self.args.gpudir).exists():
      raise ValueError(f"GPU directory does not exist: {self.args.gpudir}")
    
    if self.args.cputhreads <= 0:
      raise ValueError("CPU threads must be positive")
    if self.args.gputhreads <= 0:
      raise ValueError("GPU threads must be positive")
    
    if self.args.maxk <= 0:
      raise ValueError("maxK must be positive")
    if self.args.dim <= 0:
      raise ValueError("Dimensions must be positive")
  
  def check_outdir(self):
    out = Path(self.args.outdir)
    out.mkdir(parents=True, exist_ok=True)
    self.log.info(f"Output directory: {out.absolute()}")
  
  def run_gpu(self):
    self.log.info("Running GPU-only analysis")
    
    bbv = Path(self.args.gpudir) / 'global.bbv'
    if not bbv.exists():
      raise FileNotFoundError(f"Required file not found: {bbv}")
    
    gpu_out = Path(self.args.gpudir) / 'gpu-perthread'
    
    if not self.args.simpoint_only:
      self.log.info("Running thread splitting for GPU")
      threadsplit.main(self.args.gputhreads, self.args.gpudir, str(gpu_out))
      
      self.log.info("Concatenating GPU vectors")
      concat_xpu_vectors.main(
        self.args.gputhreads, 
        self.args.cpudir, 
        str(gpu_out), 
        str(gpu_out), 
        "gpu"
      )
    
    self.log.info("Running SimPoint clustering")
    run_simpoint.main(
      self.args.maxk, 
      self.args.dim, 
      str(gpu_out / 'global.bbv'), 
      self.args.outdir, 
      gpu_only=True, 
      fixed_length=self.args.fixed_length,
      simpoint_bin=self.args.simpoint_bin
    )
    
    self.log.info("Generating instruction weights")
    gen_insweights.main(self.args.outdir, str(gpu_out / 'global.bbv'))
  
  def run_full(self):
    self.log.info("Running XPU analysis")
    
    if not self.args.simpoint_only:
      gpu_out = Path(self.args.gpudir) / 'gpu-perthread'
    
      ngputhreads = threadsplit.get_num_threads(self.args.gpudir)
      self.log.info("Running thread splitting for GPU")
      #threadsplit.main(self.args.gputhreads, self.args.gpudir, str(gpu_out))
      threadsplit.main(ngputhreads, self.args.gpudir, str(gpu_out))
      
      self.log.info("Concatenating GPU vectors")
      concat_xpu_vectors.main(
        self.args.gputhreads, 
        self.args.cpudir, 
        str(gpu_out), 
        str(gpu_out), 
        "gpu"
      )
      
      self.log.info("Concatenating XPU vectors")
      concat_xpu_vectors.main(
        self.args.cputhreads + 1, 
        self.args.cpudir, 
        str(gpu_out), 
        self.args.outdir, 
        "xpu"
      )
    
    hv = Path(self.args.outdir) / 'T.global.hv'
    self.log.info("Running SimPoint clustering")
    run_simpoint.main(
      self.args.maxk, 
      self.args.dim, 
      str(hv), 
      self.args.outdir, 
      fixed_length=self.args.fixed_length,
      simpoint_bin=self.args.simpoint_bin
    )
    
    self.log.info("Generating instruction weights")
    gen_insweights.main(self.args.outdir)
  
  def run(self):
    try:
      self.check_outdir()
      
      if self.args.gpu_only:
        self.run_gpu()
      else:
        self.run_full()
      
      self.log.info("Analysis completed successfully")
      
    except Exception as e:
      self.log.error(f"Analysis failed: {e}")
      if self.args.verbose:
        raise
      sys.exit(1)


def get_args():
  p = argparse.ArgumentParser(
    description="Run XPU-Point post-profile analysis",
    formatter_class=argparse.ArgumentDefaultsHelpFormatter
  )
  
  tg = p.add_argument_group('Thread Configuration')
  tg.add_argument(
    "-n", "--cputhreads", 
    type=int, 
    default=8, 
    help="Number of CPU threads"
  )
  tg.add_argument(
    "-w", "--gputhreads", 
    type=int, 
    default=32, 
    help="Number of GPU threads"
  )
  
  dg = p.add_argument_group('Directory Configuration')
  dg.add_argument(
    "-c", "--cpudir", 
    type=str, 
    default=".", 
    help="CPU profile directory"
  )
  dg.add_argument(
    "-g", "--gpudir", 
    type=str, 
    default=".", 
    help="GPU profile directory"
  )
  dg.add_argument(
    "-o", "--outdir", 
    type=str, 
    help="Output directory (defaults to cpudir or gpudir based on mode)"
  )
  
  pg = p.add_argument_group('Processing Options')
  pg.add_argument(
    "--gpu-only", 
    action='store_true', 
    help="Use only GPU regions for clustering"
  )
  pg.add_argument(
    "--simpoint-only", 
    action='store_true', 
    help="Run only SimPoint clustering (skip preprocessing)"
  )
  
  sg = p.add_argument_group('SimPoint Parameters')
  sg.add_argument(
    "-m", "--maxk", 
    type=int, 
    default=20, 
    help="MaxK for K-means clustering"
  )
  sg.add_argument(
    "-d", "--dim", 
    type=int, 
    default=128, 
    help="Number of reduced dimensions"
  )
  sg.add_argument(
    "--fixed-length", 
    type=str, 
    default="on", 
    choices=["on", "off"],
    help="SimPoint fixedLength parameter"
  )
  sg.add_argument(
    "--simpoint-bin",
    type=str,
    default="",
    help="Path to SimPoint binary"
  )

  p.add_argument(
    "-v", "--verbose", 
    action='store_true', 
    help="Enable verbose output"
  )
  
  args = p.parse_args()
  
  if not args.outdir:
    args.outdir = args.gpudir if args.gpu_only else args.cpudir
  
  return args


def main():
  try:
    args = get_args()
    r = Runner(args)
    r.run()
  except KeyboardInterrupt:
    print("\nOperation cancelled by user")
    sys.exit(1)
  except Exception as e:
    print(f"Fatal error: {e}")
    sys.exit(1)


if __name__ == '__main__':
  main()
