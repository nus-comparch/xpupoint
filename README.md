# XPU-Point

A toolkit for profiling and performance evaluation of heterogeneous CPU-GPU applications across NVIDIA and Intel GPU systems.

## Overview

XPU-Point provides the necessary tools and scripts to analyze heterogeneous applications through three main components:

1. **XPU-Profiler** - Profiles heterogeneous applications and collects basic block vectors for similarity analysis
2. **XPU-Timer** - Evaluates performance of selected regions 
3. **Performance Extrapolation** - Extrapolates performance results and generates visualization plots

This repository includes an example benchmark (GROMACS) to demonstrate the end-to-end methodology.

## Quick Start

### Prerequisites

**Hardware Requirements:**
- Linux x86 system with NVIDIA GPU
  - CUDA version: >= 8.0 && <= 11.x
  - CUDA driver version: <= 495.xx
- Linux x86 system with Intel GPU
- At least 500 GB of free disk space

**Software Dependencies:**
- Docker
- NVIDIA drivers
- Intel drivers  
- CUDA
- oneAPI

### Installation

1. Clone the repository:
```bash
git clone https://github.com/nus-comparch/xpupoint && cd xpupoint
```

2. Set up the environment using Docker:
```bash
# Build the docker image
make docker.build

# Run the docker image  
make docker.run

# Compile XPU-Point tools and benchmarks
make
```

### Running Experiments

1. Navigate to the one of the benchmark directories (for example, GROMACS):
```bash
cd benchmarks/gromacs
```

2. Run XPU-Point analysis:
```bash
# Run all test cases (NOTE: The full test suite can take a long time to complete)
./run-xpupoint all

# Or run individual tests (--help shows the available tests)
./run-xpupoint <test_directory_name>
```

3. Generate visualization:
```bash
./make-graphs
```

## How It Works

The `run-xpupoint` script automates the complete analysis workflow:

1. **XPU-Profiler** identifies representative regions in the application
2. **XPU-Timer** measures performance of both the full application and individual representative regions
3. Results are processed and can be visualized as tables or graphs

## System Requirements

| Component | Requirement |
|-----------|------------|
| **Programs** | C++ programs, Python/Shell scripts |
| **Compilation** | CUDA, oneAPI, Make, GCC |
| **Binaries** | Pin 3.30, GTPin 4.5.0, NVBit 1.5.5 |
| **Runtime** | NVIDIA and Intel GPU Drivers |
| **Hardware** | NVIDIA GPU systems and Intel GPU systems |
| **Metrics** | Cycles, RDTSC, Runtime |
| **Disk Space** | ~500 GB |
| **Setup Time** | ~1 day |
| **Experiment Time** | ~1 week |

## Verification

Before running experiments, verify your GPU platforms are functioning:

```bash
# For NVIDIA GPUs
nvidia-smi

# For Intel GPUs  
sycl-ls
```

## Important Notes

**Performance Considerations:**
- Results are closely tied to the specific execution environment
- XPU-Timer results may be affected by other jobs running on the same machine
- Profiling runs for larger applications can take several hours

## Availability

- **GitHub Repository**: [nus-comparch/xpupoint](https://github.com/nus-comparch/xpupoint)
- **Archived Version**: [Zenodo DOI: 10.5281/zenodo.16801115](https://doi.org/10.5281/zenodo.16801115)
- **License**: Open source (see repository for specific license details)

## Contributing

This project is publicly available and contributions are welcome. Please check the repository for contribution guidelines.

## Support

For issues and questions, please use the GitHub Issues section of this repository.
