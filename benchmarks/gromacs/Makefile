# Makefile.benchmark - GROMACS benchmark

GROMACS_VERSION := 2023.3
GROMACS_URL := https://ftp.gromacs.org/gromacs/gromacs-$(GROMACS_VERSION).tar.gz
CUR_DIR := $(shell dirname $(realpath $(lastword $(MAKEFILE_LIST))))
GROMACS_DIR := $(CUR_DIR)/gromacs-$(GROMACS_VERSION)
BUILD_DIR := $(GROMACS_DIR)/build
INSTALL_DIR := $(CUR_DIR)/install

ifndef PROJ_ROOT
	PROJ_ROOT := $(abspath $(CUR_DIR)/../..)
endif
TOOLS_DIR := $(PROJ_ROOT)/tools
CMAKE_VERSION := 3.30.7
CMAKE_URL := https://github.com/Kitware/CMake/releases/download/v$(CMAKE_VERSION)/cmake-$(CMAKE_VERSION)-linux-x86_64.tar.gz
CMAKE_DIR := $(TOOLS_DIR)/cmake-$(CMAKE_VERSION)-linux-x86_64
CMAKE_BIN := $(CMAKE_DIR)/bin/cmake

INPUT_DIR := $(CUR_DIR)/input
BENCHMEM_URL := https://www.mpinat.mpg.de/benchMEM
BENCHMEM_ZIP := benchMEM.zip
BENCHMEM_FILE := benchMEM.tpr

SCRIPTS_DIR ?=

.PHONY: all help download build-intel build-nvidia benchmem tests tests-intel tests-nvidia
.PHONY: intel nvidia cmake clean-install clean-benchmem clean-tests clean-cmake clean

all: help

download:
	@if [ ! -d "$(GROMACS_DIR)" ]; then \
		echo "Downloading GROMACS $(GROMACS_VERSION)"; \
		wget -O - $(GROMACS_URL) --no-check-certificate --quiet --show-progress | tar -xf - -z ; \
	fi

cmake: $(CMAKE_BIN)

$(CMAKE_BIN):
	@echo "Downloading CMAKE $(CMAKE_VERSION)"; \
	wget -O - $(CMAKE_URL) --no-check-certificate --quiet --show-progress | tar -xf - -z -C $(TOOLS_DIR) ; \

build-intel: download cmake
	@mkdir -p $(INSTALL_DIR)
	@mkdir -p $(BUILD_DIR)
	@cd $(BUILD_DIR) && \
	$(CMAKE_BIN) .. \
		-DGMX_GPU=SYCL \
		-DGMX_BUILD_OWN_FFTW=ON \
		-DCMAKE_CXX_COMPILER=icpx \
		-DCMAKE_C_COMPILER=icx \
		-DCMAKE_INSTALL_PREFIX=$(INSTALL_DIR) \
		-DGMXAPI=OFF \
		-DGMX_GPU_NB_CLUSTER_SIZE=8 \
		-DGMX_GPU_NB_NUM_CLUSTER_PER_CELL_X=1 \
		-DGMX_FFT_LIBRARY=mkl \
		-DCMAKE_DISABLE_FIND_PACKAGE_MPI=on && \
	$(MAKE) -j$(shell nproc) && \
	$(MAKE) install

build-nvidia: download cmake
	@mkdir -p $(INSTALL_DIR)
	@mkdir -p $(BUILD_DIR)
	@cd $(BUILD_DIR) && \
	$(CMAKE_BIN) .. \
		-DGMX_GPU=CUDA \
		-DGMX_BUILD_OWN_FFTW=ON \
		-DCMAKE_INSTALL_PREFIX=$(INSTALL_DIR) \
		-DGMXAPI=OFF \
		-DGMX_GPU_NB_CLUSTER_SIZE=8 \
		-DGMX_GPU_NB_NUM_CLUSTER_PER_CELL_X=2 \
		-DGMX_FFT_LIBRARY=fftw3 \
		-DCMAKE_DISABLE_FIND_PACKAGE_MPI=on && \
	$(MAKE) -j$(shell nproc) && \
	$(MAKE) install

benchmem:
	@mkdir -p $(INPUT_DIR)
	@cd $(INPUT_DIR) && \
	if [ ! -f "$(BENCHMEM_FILE)" ]; then \
		wget -O $(BENCHMEM_ZIP) $(BENCHMEM_URL) --no-check-certificate --quiet --show-progress; \
		if [ -f "$(BENCHMEM_ZIP)" ]; then \
			unzip -q $(BENCHMEM_ZIP) || echo "Note: unzip failed, please extract manually"; \
			rm -f $(BENCHMEM_ZIP); \
		else \
			echo "Failed to download benchMEM. Please download manually from $(BENCHMEM_URL)"; \
		fi; \
	fi

tests:
	@for testcase in tests/*/; do \
		if [ -d "$$testcase" ]; then \
			cp -r input/* "$$testcase/"; \
			cp -r scripts/$(SCRIPTS_DIR)/* "$$testcase/"; \
			for script in "$$testcase"/run*.sh; do \
				if [ -f "$$script" ]; then \
					sed -i '2i PROJ_ROOT=$(PROJ_ROOT)' "$$script"; \
					sed -i '2i source $(INSTALL_DIR)/bin/GMXRC' "$$script"; \
				fi; \
			done; \
		fi; \
	done

tests-intel: SCRIPTS_DIR=intel
tests-intel: tests

tests-nvidia: SCRIPTS_DIR=nvidia
tests-nvidia: tests

intel: build-intel benchmem tests-intel

nvidia: build-nvidia benchmem tests-nvidia

clean-install:
	-rm -rf $(BUILD_DIR)
	-rm -rf $(INSTALL_DIR)

clean-benchmem:
	-rm -rf $(INPUT_DIR)

clean-cmake:
	-rm -rf $(CMAKE_DIR)

clean-tests:
	@for testcase in tests/*/; do \
		if [ -d "$$testcase" ]; then \
			find "$$testcase" -mindepth 1 -name "COMMAND.sh" -prune -o -type f -exec rm -f {} \; -o -type d -exec rm -rf {} \; 2>/dev/null || true; \
		fi; \
	done
	-rm -f *.png

clean: clean-install clean-tests

distclean: clean-install clean-benchmem clean-tests
	-rm -rf $(GROMACS_DIR)

help:
	@echo "GROMACS Benchmark Setup"
	@echo "======================="
	@echo ""
	@echo "Platform targets:"
	@echo "  intel           - Build and setup benchmark for Intel GPU"
	@echo "  nvidia          - Build and setup benchmark for NVIDIA GPU"
	@echo ""
	@echo "Setup targets:"
	@echo "  download      - Download GROMACS source code"
	@echo "  build-intel   - Build GROMACS for Intel GPU"
	@echo "  build-nvidia  - Build GROMACS for NVIDIA GPU"
	@echo "  benchmem      - Download benchMEM"
	@echo "  tests-intel   - Setup test cases for Intel GPU"
	@echo "  tests-nvidia  - Setup test cases for NVIDIA GPU"
	@echo ""
	@echo "Cleaning targets:"
	@echo "  clean-install   - Clean GROMACS build artifacts"
	@echo "  clean-benchmem  - Clean benchMEM files"
	@echo "  clean-tests     - Clean test case directory files"
	@echo "  clean           - Clean everything including source"
