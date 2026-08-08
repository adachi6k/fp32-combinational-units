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
.PHONY: all test smoke stress add sub mul div sqrt cmp div-build div-test \
	sqrt-build sqrt-test debug_div clean softfloat lint cli-test
all: test
test: add sub mul cmp div-test sqrt-test
smoke:
	$(MAKE) test FP32_NUM_TESTS=1000 FP32_SEED=$${FP32_SEED:-1}
	$(MAKE) cli-test
stress:
	$(MAKE) test FP32_NUM_TESTS=60000000 FP32_SEED=$${FP32_SEED:-1}

# Validate that invalid CLI arguments are rejected with a non-zero exit code.
define check_cli_error
	@printf 'cli-test: %-50s' "$(strip $(2))"; \
	if $(1) >/dev/null 2>&1; then \
	  echo "FAIL (expected non-zero exit)"; exit 1; \
	else \
	  echo "ok"; \
	fi
endef

cli-test: add sub mul cmp div-build sqrt-build
	$(call check_cli_error, ./obj_dir/add/Vfp32_add_comb --tests,          add: --tests without value)
	$(call check_cli_error, ./obj_dir/add/Vfp32_add_comb --seed,           add: --seed without value)
	$(call check_cli_error, ./obj_dir/add/Vfp32_add_comb --unknown,        add: unknown option)
	$(call check_cli_error, ./obj_dir/add/Vfp32_add_comb not_a_number,     add: non-numeric positional)
	$(call check_cli_error, FP32_NUM_TESTS=bad ./obj_dir/add/Vfp32_add_comb, add: invalid FP32_NUM_TESTS)
	$(call check_cli_error, ./obj_dir/sub/Vfp32_sub_comb --tests,          sub: --tests without value)
	$(call check_cli_error, ./obj_dir/sub/Vfp32_sub_comb --seed,           sub: --seed without value)
	$(call check_cli_error, ./obj_dir/sub/Vfp32_sub_comb --unknown,        sub: unknown option)
	$(call check_cli_error, ./obj_dir/sub/Vfp32_sub_comb not_a_number,     sub: non-numeric positional)
	$(call check_cli_error, FP32_SEED=bad ./obj_dir/sub/Vfp32_sub_comb,    sub: invalid FP32_SEED)
	$(call check_cli_error, ./obj_dir/mul/Vfp32_mul_comb --tests,          mul: --tests without value)
	$(call check_cli_error, ./obj_dir/mul/Vfp32_mul_comb --seed,           mul: --seed without value)
	$(call check_cli_error, ./obj_dir/mul/Vfp32_mul_comb --unknown,        mul: unknown option)
	$(call check_cli_error, ./obj_dir/mul/Vfp32_mul_comb not_a_number,     mul: non-numeric positional)
	$(call check_cli_error, ./obj_dir/cmp/Vfp32_cmp_comb --tests,          cmp: --tests without value)
	$(call check_cli_error, ./obj_dir/cmp/Vfp32_cmp_comb --seed,           cmp: --seed without value)
	$(call check_cli_error, ./obj_dir/cmp/Vfp32_cmp_comb --unknown,        cmp: unknown option)
	$(call check_cli_error, ./obj_dir/cmp/Vfp32_cmp_comb not_a_number,     cmp: non-numeric positional)
	$(call check_cli_error, ./obj_dir/div/Vfp32_div_comb --tests,          div: --tests without value)
	$(call check_cli_error, ./obj_dir/div/Vfp32_div_comb --seed,           div: --seed without value)
	$(call check_cli_error, ./obj_dir/div/Vfp32_div_comb --unknown,        div: unknown option)
	$(call check_cli_error, ./obj_dir/div/Vfp32_div_comb not_a_number,     div: non-numeric positional)
	$(call check_cli_error, FP32_NUM_TESTS=bad ./obj_dir/div/Vfp32_div_comb, div: invalid FP32_NUM_TESTS)
	$(call check_cli_error, ./obj_dir/sqrt/Vfp32_sqrt_comb --tests,        sqrt: --tests without value)
	$(call check_cli_error, ./obj_dir/sqrt/Vfp32_sqrt_comb --seed,         sqrt: --seed without value)
	$(call check_cli_error, ./obj_dir/sqrt/Vfp32_sqrt_comb --unknown,      sqrt: unknown option)
	$(call check_cli_error, ./obj_dir/sqrt/Vfp32_sqrt_comb not_a_number,   sqrt: non-numeric positional)
	$(call check_cli_error, FP32_SEED=bad ./obj_dir/sqrt/Vfp32_sqrt_comb,  sqrt: invalid FP32_SEED)
	@echo "cli-test: all invalid-argument checks passed"

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
add: softfloat | obj_dir/add
	$(VERILATOR) --threads 4 --top-module fp32_add_comb --build --cc fp32_add_comb.sv fp32_addsub.sv \
		--exe $(ROOTDIR)/tb_fp32_add_comb.cpp --Mdir obj_dir/add -CFLAGS "$(CFLAGS)" -LDFLAGS "$(LDFLAGS)"
	./obj_dir/add/Vfp32_add_comb

# Build and run fp32_sub_comb testbench (subtractor wrapper + shared add/sub core)
sub: softfloat | obj_dir/sub
	$(VERILATOR) --threads 4 --top-module fp32_sub_comb --build --cc fp32_sub_comb.sv fp32_addsub.sv \
		--exe $(ROOTDIR)/tb_fp32_sub_comb.cpp --Mdir obj_dir/sub -CFLAGS "$(CFLAGS)" -LDFLAGS "$(LDFLAGS)"
	./obj_dir/sub/Vfp32_sub_comb

# Build and run fp32_mul_comb testbench (independent multiplier)
mul: softfloat | obj_dir/mul
	$(VERILATOR) --threads 4 --top-module fp32_mul_comb --build --cc fp32_mul_comb.sv \
		--exe $(ROOTDIR)/tb_fp32_mul_comb.cpp --Mdir obj_dir/mul -CFLAGS "$(CFLAGS)" -LDFLAGS "$(LDFLAGS)"
	./obj_dir/mul/Vfp32_mul_comb

# Build and run the DW_fp_cmp-style FP32 comparator testbench
cmp: | obj_dir/cmp
	$(VERILATOR) --threads 4 --top-module fp32_cmp_comb --build --cc fp32_cmp_comb.sv \
		--exe $(ROOTDIR)/tb_fp32_cmp_comb.cpp --Mdir obj_dir/cmp
	./obj_dir/cmp/Vfp32_cmp_comb

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
