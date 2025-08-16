# Main Makefile

.PHONY: auto Intel intel INTEL NVIDIA nvidia Nvidia help clean detect-gpu distclean \
		clean-intel clean-nvidia benchmark bench-intel bench-nvidia clean-bench \
		$(addprefix build-intel-,$(BENCHMARKS)) \
		$(addprefix build-nvidia-,$(BENCHMARKS)) \
		$(addprefix clean-bench-,$(BENCHMARKS))

BENCHMARK_DIRS := $(wildcard benchmarks/*)
BENCHMARKS := $(notdir $(BENCHMARK_DIRS))

auto: detect-gpu

detect-gpu:
	@if command -v nvidia-smi >/dev/null 2>&1 && nvidia-smi >/dev/null 2>&1; then \
		echo "NVIDIA GPU detected, building with NVIDIA configuration..."; \
		$(MAKE) -f Makefile.NVIDIA; \
		$(MAKE) bench-nvidia; \
	elif command -v intel_gpu_top >/dev/null 2>&1 || lspci | grep -i "vga.*intel" >/dev/null 2>&1; then \
		echo "Intel GPU detected, building with Intel configuration..."; \
		$(MAKE) -f Makefile.Intel; \
		$(MAKE) bench-intel; \
	elif lspci | grep -i "display.*intel" >/dev/null 2>&1; then \
		echo "Intel graphics detected, building with Intel configuration..."; \
		$(MAKE) -f Makefile.Intel; \
		$(MAKE) bench-intel; \
	else \
		echo "No specific GPU detected or unsupported GPU."; \
		echo "Please manually specify: 'make Intel' or 'make NVIDIA'"; \
		echo "Available GPUs:"; \
		lspci | grep -i -E "(vga|display|3d)" || echo "  No graphics devices found"; \
		exit 1; \
	fi

Intel:
	$(MAKE) -f Makefile.Intel
	$(MAKE) bench-intel

intel: Intel
INTEL: Intel

NVIDIA:
	$(MAKE) -f Makefile.NVIDIA
	$(MAKE) bench-nvidia

nvidia: NVIDIA
Nvidia: NVIDIA

bench-intel: $(addprefix build-intel-,$(BENCHMARKS))

bench-nvidia: $(addprefix build-nvidia-,$(BENCHMARKS))

build-intel-%:
	$(MAKE) -C "benchmarks/$*" -f "Makefile" intel

build-nvidia-%:
	$(MAKE) -C "benchmarks/$*" -f "Makefile" nvidia

benchmark:
	@echo "Benchmarks: $(BENCHMARKS)"

benchmarks: benchmark

clean: clean-intel clean-nvidia 

distclean:
	-$(MAKE) -f Makefile.Intel distclean
	-$(MAKE) -f Makefile.NVIDIA distclean
	-$(MAKE) -C "benchmarks/$*" -f "Makefile" distclean

clean-intel:
	-$(MAKE) -f Makefile.Intel clean

clean-nvidia:
	-$(MAKE) -f Makefile.NVIDIA clean

clean-bench: $(addprefix clean-bench-,$(BENCHMARKS))

clean-bench-%:
	-$(MAKE) -C "benchmarks/$*" -f "Makefile" clean

docker:
		$(MAKE) -f Makefile.docker help

# Usage: make docker.<command>, e.g., make docker.build, make docker.run
docker.%:
	$(MAKE) -f Makefile.docker $*

help:
	@echo "XPU-Point Build System"
	@echo "======================"
	@echo ""
	@echo "Auto-detection:"
	@echo "  make         - Auto-detect GPU, build appropriate tools and benchmarks"
	@echo "  make auto    - Same as above"
	@echo ""
	@echo "Manual selection:"
	@echo "  make Intel   - Build using Intel-specific configuration"
	@echo "  make NVIDIA  - Build using NVIDIA-specific configuration"
	@echo ""
	@echo "Docker Management:"
	@echo "  make docker.build	- Build the default Docker image"
	@echo "  make docker.run    - Run the default Docker image"
	@echo ""
	@echo "Cleaning:"
	@echo "  make clean       - Clean all build artifacts (Intel + NVIDIA)"
	@echo "  make clean-intel - Clean only Intel build artifacts"
	@echo "  make clean-nvidia - Clean only NVIDIA build artifacts"
	@echo ""
	@echo "Specific benchmark compilation/execution (advanced):"
	@echo "  make benchmark      - Show benchmark setup options and available benchmarks"
	@echo "  make benchmark.<name>.<target> - Run a specific target for a given benchmark"
	@echo ""
	@echo "Advanced usage (pass targets to sub-makefiles):"
	@echo "  make Intel.cpupintool   - Build only Intel CPUPinTool"
	@echo "  make Intel.gtpintool    - Build only Intel GTPinTool"
	@echo "  make NVIDIA.cpupintool  - Build only NVIDIA CPUPinTool"
	@echo "  make NVIDIA.nvbittool   - Build only NVIDIA NVBitTool"
	@echo "  make Intel.clean-deps   - Clean only Intel dependencies"
	@echo "  make NVIDIA.setup-deps  - Download only NVIDIA dependencies"
	@echo ""
	@echo "For more specific help:"
	@echo "  make Intel.help   - Show Intel-specific targets"
	@echo "  make NVIDIA.help  - Show NVIDIA-specific targets"
	@echo "  make docker.help  - Show docker-specific targets"

Intel.%:
	$(MAKE) -f Makefile.Intel $*

NVIDIA.%:
	$(MAKE) -f Makefile.NVIDIA $*

benchmark.%:
	@if [ -d "benchmarks/$(word 1,$(subst ., ,$*))" ]; then \
		bm=$(word 1,$(subst ., ,$*)); \
		target=$(word 2,$(subst ., ,$*)); \
		if [ -z "$$target" ]; then \
			echo "Error: Please specify a target"; \
			exit 1; \
		fi; \
		$(MAKE) -C "benchmarks/$$bm" -f "Makefile" "$$target"; \
	else \
		echo "Error: Unknown benchmark '$*'."; \
		exit 1; \
	fi
