# Simple Makefile for fp32 add, sub, mul, div and sqrt with SoftFloat
# Based on README.md instructions

VERILATOR = verilator
CC        = gcc
CXX       = g++

# SoftFloat specialization (RISCV canonical NaN, see README for details)
SPECIALIZE_TYPE ?= RISCV

# Paths to SoftFloat
ROOTDIR := $(shell pwd)
SOFT_INCLUDE_DIR := $(ROOTDIR)/softfloat/source/include
SOFT_BUILD_DIR   := $(ROOTDIR)/softfloat/build/Linux-x86_64-GCC
SOFT_LIB         := $(SOFT_BUILD_DIR)/softfloat.a

# Compiler flags: include SoftFloat headers and build directory (for platform.h)
CFLAGS    = -I$(SOFT_INCLUDE_DIR) -I$(SOFT_BUILD_DIR)
LDFLAGS   = -L$(SOFT_BUILD_DIR) -l:softfloat.a

# Number of stratified random vectors used by the add/sub/mul testbenches.
# Override on the command line, e.g. `make add FP32_NUM_TESTS=50000000`.
# It is exported so the Verilated testbench executables pick it up.
FP32_NUM_TESTS ?= 2000000
export FP32_NUM_TESTS

# Targets
.PHONY: all add sub mul div sqrt cmp debug_div clean softfloat
all: div sqrt add sub mul cmp

# Build SoftFloat reference library with the chosen specialization
softfloat: $(SOFT_LIB)

$(SOFT_LIB):
	$(MAKE) -C $(SOFT_BUILD_DIR) SPECIALIZE_TYPE=$(SPECIALIZE_TYPE)

# Build and run fp32_add_comb testbench (adder wrapper + shared add/sub core)
add: $(SOFT_LIB)
	$(VERILATOR) --threads 4 --top-module fp32_add_comb --build --cc fp32_add_comb.sv fp32_addsub.sv \
		--exe tb_fp32_add_comb.cpp -CFLAGS "$(CFLAGS)" -LDFLAGS "$(LDFLAGS)"
	./obj_dir/Vfp32_add_comb

# Build and run fp32_sub_comb testbench (subtractor wrapper + shared add/sub core)
sub: $(SOFT_LIB)
	$(VERILATOR) --threads 4 --top-module fp32_sub_comb --build --cc fp32_sub_comb.sv fp32_addsub.sv \
		--exe tb_fp32_sub_comb.cpp -CFLAGS "$(CFLAGS)" -LDFLAGS "$(LDFLAGS)"
	./obj_dir/Vfp32_sub_comb

# Build and run fp32_mul_comb testbench (independent multiplier)
mul: $(SOFT_LIB)
	$(VERILATOR) --threads 4 --top-module fp32_mul_comb --build --cc fp32_mul_comb.sv \
		--exe tb_fp32_mul_comb.cpp -CFLAGS "$(CFLAGS)" -LDFLAGS "$(LDFLAGS)"
	./obj_dir/Vfp32_mul_comb

# Build and run the DW_fp_cmp-style FP32 comparator testbench
cmp:
	$(VERILATOR) --threads 4 --top-module fp32_cmp_comb --build --cc fp32_cmp_comb.sv \
		--exe tb_fp32_cmp_comb.cpp
	./obj_dir/Vfp32_cmp_comb

# Build fp32_div_comb testbench
div:
	$(VERILATOR) --threads 4 --top-module fp32_div_comb --build --cc fp32_div_comb.sv fp32_sqrt_comb.sv \
		--exe tb_fp32_div_comb.cpp -CFLAGS "$(CFLAGS)" -LDFLAGS "$(LDFLAGS)"

# Build fp32_sqrt_comb testbench
sqrt:
	$(VERILATOR) --threads 4 --top-module fp32_sqrt_comb --build --cc fp32_sqrt_comb.sv \
		--exe tb_fp32_sqrt_comb.cpp -CFLAGS "$(CFLAGS)" -LDFLAGS "$(LDFLAGS)"

# Build debug version for specific cases
debug_div:
	$(VERILATOR) --threads 4 --top-module fp32_div_comb --build --cc fp32_div_comb.sv \
		--exe debug_div.cpp -CFLAGS "$(CFLAGS)" -LDFLAGS "$(LDFLAGS)"

# Clean artifacts
clean:
	rm -rf obj_dir
	rm -f Vfp32_div_comb Vfp32_sqrt_comb Vfp32_add_comb Vfp32_sub_comb Vfp32_mul_comb Vfp32_cmp_comb
