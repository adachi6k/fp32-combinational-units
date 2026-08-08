# Simple Makefile for combinational FP32 arithmetic units with SoftFloat
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
SOFT_CONFIG_STAMP := $(ROOTDIR)/.softfloat-specialize-$(SPECIALIZE_TYPE)

# Compiler flags: include SoftFloat headers and build directory (for platform.h)
CFLAGS    = -I$(SOFT_INCLUDE_DIR) -I$(SOFT_BUILD_DIR)
LDFLAGS   = -L$(SOFT_BUILD_DIR) -l:softfloat.a

# Optional randomized test configuration, exported so the Verilated
# testbench executables can pick it up when provided.
export FP32_NUM_TESTS
export FP32_SEED

OBJ_DIRS := obj_dir/add obj_dir/sub obj_dir/mul obj_dir/cmp obj_dir/div obj_dir/sqrt \
	obj_dir/debug_div

$(OBJ_DIRS):
	mkdir -p $@

# Targets
.PHONY: all test smoke stress add sub mul div sqrt cmp \
	add-build add-test sub-build sub-test mul-build mul-test cmp-build cmp-test \
	div-build div-test sqrt-build sqrt-test debug_div clean softfloat lint cli-test
all: test
test: add-test sub-test mul-test cmp-test div-test sqrt-test
smoke:
	$(MAKE) test cli-test FP32_NUM_TESTS=1000 FP32_SEED=$${FP32_SEED:-1}
stress:
	$(MAKE) test FP32_NUM_TESTS=60000000 FP32_SEED=$${FP32_SEED:-1}

# Validate that invalid CLI arguments are rejected with a non-zero exit code.
cli-test: add-build sub-build mul-build cmp-build div-build sqrt-build
	bash scripts/test_cli_args.sh

# A specialization switch must invalidate objects because SoftFloat reuses the
# same object names for every specialization.  Source-only changes are handled
# by the recursive make's own dependency graph.
$(SOFT_CONFIG_STAMP):
	rm -f $(ROOTDIR)/.softfloat-specialize-*
	$(MAKE) -C $(SOFT_BUILD_DIR) clean
	touch $@

softfloat: $(SOFT_CONFIG_STAMP)
	$(MAKE) -C $(SOFT_BUILD_DIR) SPECIALIZE_TYPE=$(SPECIALIZE_TYPE)

# Build and run fp32_add_comb testbench (adder wrapper + shared add/sub core)
add-build: softfloat | obj_dir/add
	$(VERILATOR) --threads 4 --top-module fp32_add_comb --build --cc fp32_add_comb.sv fp32_addsub.sv \
		--exe $(ROOTDIR)/tb_fp32_add_comb.cpp --Mdir obj_dir/add -CFLAGS "$(CFLAGS)" -LDFLAGS "$(LDFLAGS)"
add-test: add-build
	./obj_dir/add/Vfp32_add_comb
add: add-test

# Build and run fp32_sub_comb testbench (subtractor wrapper + shared add/sub core)
sub-build: softfloat | obj_dir/sub
	$(VERILATOR) --threads 4 --top-module fp32_sub_comb --build --cc fp32_sub_comb.sv fp32_addsub.sv \
		--exe $(ROOTDIR)/tb_fp32_sub_comb.cpp --Mdir obj_dir/sub -CFLAGS "$(CFLAGS)" -LDFLAGS "$(LDFLAGS)"
sub-test: sub-build
	./obj_dir/sub/Vfp32_sub_comb
sub: sub-test

# Build and run fp32_mul_comb testbench (independent multiplier)
mul-build: softfloat | obj_dir/mul
	$(VERILATOR) --threads 4 --top-module fp32_mul_comb --build --cc fp32_mul_comb.sv \
		--exe $(ROOTDIR)/tb_fp32_mul_comb.cpp --Mdir obj_dir/mul -CFLAGS "$(CFLAGS)" -LDFLAGS "$(LDFLAGS)"
mul-test: mul-build
	./obj_dir/mul/Vfp32_mul_comb
mul: mul-test

# Build and run the DW_fp_cmp-style FP32 comparator testbench
cmp-build: | obj_dir/cmp
	$(VERILATOR) --threads 4 --top-module fp32_cmp_comb --build --cc fp32_cmp_comb.sv \
		--exe $(ROOTDIR)/tb_fp32_cmp_comb.cpp --Mdir obj_dir/cmp
cmp-test: cmp-build
	./obj_dir/cmp/Vfp32_cmp_comb
cmp: cmp-test

# Build fp32_div_comb testbench
div-build: softfloat | obj_dir/div
	$(VERILATOR) --threads 4 --top-module fp32_div_comb --build --cc fp32_div_comb.sv \
		--exe $(ROOTDIR)/tb_fp32_div_comb.cpp --Mdir obj_dir/div -CFLAGS "$(CFLAGS)" -LDFLAGS "$(LDFLAGS)"
div-test: div-build
	./obj_dir/div/Vfp32_div_comb
div: div-test

# Build fp32_sqrt_comb testbench
sqrt-build: softfloat | obj_dir/sqrt
	$(VERILATOR) --threads 4 --top-module fp32_sqrt_comb --build --cc fp32_sqrt_comb.sv \
		--exe $(ROOTDIR)/tb_fp32_sqrt_comb.cpp --Mdir obj_dir/sqrt -CFLAGS "$(CFLAGS)" -LDFLAGS "$(LDFLAGS)"
sqrt-test: sqrt-build
	./obj_dir/sqrt/Vfp32_sqrt_comb
sqrt: sqrt-test

# Build debug version for specific cases
debug_div: softfloat | obj_dir/debug_div
	$(VERILATOR) --threads 4 --top-module fp32_div_comb --build --cc fp32_div_comb.sv \
		--exe $(ROOTDIR)/debug_div.cpp --Mdir obj_dir/debug_div -CFLAGS "$(CFLAGS)" -LDFLAGS "$(LDFLAGS)"

lint:
	$(VERILATOR) --lint-only --Wall --top-module fp32_add_comb fp32_add_comb.sv fp32_addsub.sv
	$(VERILATOR) --lint-only --Wall --top-module fp32_sub_comb fp32_sub_comb.sv fp32_addsub.sv
	$(VERILATOR) --lint-only --Wall --top-module fp32_mul_comb fp32_mul_comb.sv
	$(VERILATOR) --lint-only --Wall --top-module fp32_cmp_comb fp32_cmp_comb.sv
	$(VERILATOR) --lint-only --Wall --top-module fp32_div_comb fp32_div_comb.sv
	$(VERILATOR) --lint-only --Wall --top-module fp32_sqrt_comb fp32_sqrt_comb.sv

# Clean artifacts
clean:
	rm -rf obj_dir
	rm -f Vfp32_div_comb Vfp32_sqrt_comb Vfp32_add_comb Vfp32_sub_comb Vfp32_mul_comb Vfp32_cmp_comb
