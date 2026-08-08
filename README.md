# IEEE-754 FP32 Combinational Arithmetic Units

[![MIT License](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)
[![SystemVerilog](https://img.shields.io/badge/Language-SystemVerilog-green.svg)]()
[![Tested](https://img.shields.io/badge/Testing-SoftFloat_Verified-brightgreen.svg)]()
[![Version](https://img.shields.io/badge/Version-1.1.0-orange.svg)](CHANGELOG.md)

A collection of IEEE-754 single-precision (FP32) combinational add, subtract,
multiply, divide, square-root, and comparison units in SystemVerilog with
reproducible SoftFloat-backed verification.

## 🎯 Overview

This project provides combinational floating-point arithmetic units together
with self-checking verification infrastructure:

- **`fp32_addsub.sv`**: Shared add/subtract datapath (`op_sub` selects `a+b` or `a-b`)
- **`fp32_add_comb.sv`**: Combinational FP32 adder (thin wrapper over the shared core)
- **`fp32_sub_comb.sv`**: Combinational FP32 subtractor (thin wrapper over the shared core)
- **`fp32_mul_comb.sv`**: Independent combinational FP32 multiplier
- **`fp32_cmp_comb.sv`**: DW_fp_cmp-style FP32 comparator with ordered min/max outputs
- **`fp32_div_comb.sv`**: Combinational FP32 divider (round-to-nearest-even only)
- **`fp32_sqrt_comb.sv`**: Combinational FP32 square-root (round-to-nearest-even only)
- **Comprehensive Verification**: Self-checking testbenches using Verilator, with SoftFloat as an
  external black-box reference oracle
  - `tb_fp32_add_comb.cpp` / `tb_fp32_sub_comb.cpp` / `tb_fp32_mul_comb.cpp`: corner, systematic
    boundary, and configurable stratified random testing against `f32_add`/`f32_sub`/`f32_mul`
  - `tb_fp32_cmp_comb.cpp`: deterministic corner and configurable randomized comparison testing
  - `tb_fp32_div_comb.cpp`: 60M+ test vectors including systematic and stratified random testing
  - `tb_fp32_sqrt_comb.cpp`: Extensive corner-case and random testing for square-root

The add/subtract and multiply units use independently authored SystemVerilog datapaths built with
conventional floating-point hardware techniques: unpack and classify the operands, work on an
effective exponent plus a 24-bit significand, normalise with a leading-zero count, retain
discarded information in guard/round/sticky bits, and apply round-to-nearest-even with gradual
underflow. These RTL units do not contain a port of a floating-point software library. Berkeley
SoftFloat is used as an external black-box reference oracle for the C++ testbenches, against
which the result and all five exception flags are checked bit-for-bit.

## ✨ Key Features

- **🚀 Pure Combinational Logic**: No clocks, resets, or flip-flops - ideal for high-throughput pipelined designs
- **📐 IEEE-754 Compliant**: Full support for special values, all exception flags, and round-to-nearest-even
- **🎯 Bit-Exact Accuracy**: Zero ULP errors - verified against the Berkeley SoftFloat reference oracle
- **🧪 Reproducible Testing**:
  - Corner cases for all IEEE-754 special values
  - Systematic boundary testing for subnormal regions
  - Stratified random testing across entire FP32 space
  - Deterministic replay with `FP32_SEED` / `--seed`
- **📋 Professional Quality**: MIT licensed, comprehensive documentation, unified coding standards

## Design Intent

- **Correctness**: Bit-exact IEEE-754 compliance including proper flag generation
- **Maintainability**: Clear, well-documented SystemVerilog with algorithmic comments
- **Verification**: Reproducible randomized and systematic SoftFloat-backed checking

## Prerequisites

- **Verilator**: For RTL simulation, lint, and testbench compilation
- **GNU Make**, **g++**, **gcc**: Standard build tools
- **SoftFloat library**: Berkeley reference implementation, used as a verification oracle only
  - Built under `softfloat/build/Linux-x86_64-GCC/` with `softfloat.a` and headers
- **Tool qualification status**:

  | Tool / frontend | Status | Continuous check | Verified on |
  |---|---|---|---|
  | Verilator | Supported | `make lint`, `make smoke` | Ubuntu 24.04 / Verilator 5.020 |
  | Icarus Verilog | Not yet qualified | No | — |
  | Yosys | Not yet qualified | No | — |

## Build & Test

1. **Build SoftFloat reference library** (RISC-V specialization):
   ```bash
   make softfloat      # Builds with SPECIALIZE_TYPE=RISCV (default)
   ```

2. **Run builds and tests**:
   ```bash
   make all      # Same as `make test`; build and run all six operation tests
   make test     # Build and run add/sub/mul/cmp/div/sqrt
   make smoke    # Fast deterministic cross-operation smoke run
   make stress   # Long deterministic run with 60M random vectors per operation
   make add      # Build & run the adder testbench
   make sub      # Build & run the subtractor testbench
   make mul      # Build & run the multiplier testbench
   make cmp      # Build & run the comparator testbench
   make div      # Alias for `make div-test`
   make sqrt     # Alias for `make sqrt-test`
   make div-build
   make div-test
   make sqrt-build
   make sqrt-test
   make cli-test # Build all benches and verify invalid CLI inputs fail fast
   make lint     # Verilator --lint-only --Wall over all six top modules
   make clean    # Clean all generated files
   ```

   The randomized vector count for every randomized testbench is configurable
   (the deterministic corner cases and systematic boundary sweeps always run).
   Counts and seeds are unsigned 64-bit integers; negative, non-numeric, and
   out-of-range values are rejected. The randomized sequence is reproducible
   through a shared seed, and all 64 seed bits affect every PRNG. Set
   `FP32_NUM_TESTS` / `--tests` and `FP32_SEED` / `--seed`:
   ```bash
   make test FP32_NUM_TESTS=100000 FP32_SEED=1
   ./obj_dir/mul/Vfp32_mul_comb --seed 7 --tests 1000000
   ./obj_dir/cmp/Vfp32_cmp_comb -v --seed 7 1000
   ```
   The add/sub/mul/cmp testbenches default to 2,000,000 random vectors; the
   div/sqrt testbenches default to 60,000,000. Any mismatch (result or
   exception flag) prints the operands, result, flags, seed, and test index and
   exits non-zero.

3. **Test output interpretation**:
   - Corner cases are tested first with detailed pass/fail reporting
   - Random testing follows with millions of test vectors
   - ULP (Unit in Last Place) differences and IEEE-754 exception flags are verified
   - Tests pass when results match SoftFloat bit-exactly

## Module Interface

### FP32 Add/Subtract Core (`fp32_addsub`)
```systemverilog
module fp32_addsub (
    input  logic [31:0] a,              // operand A (IEEE-754 FP32)
    input  logic [31:0] b,              // operand B (IEEE-754 FP32)
    input  logic        op_sub,         // 0 => a + b, 1 => a - b
    output logic        exc_invalid,    // invalid operation (inf-inf, sNaN, ...)
    output logic        exc_divzero,    // divide-by-zero (tied low for add/sub)
    output logic        exc_overflow,   // overflow flag
    output logic        exc_underflow,  // underflow flag
    output logic        exc_inexact,    // inexact result flag
    output logic [31:0] y               // a +/- b (IEEE-754 FP32)
);
```

### FP32 Adder (`fp32_add_comb`) / Subtractor (`fp32_sub_comb`)
Thin wrappers that instantiate `fp32_addsub` with `op_sub` tied low / high:
```systemverilog
module fp32_add_comb (   // and fp32_sub_comb, identical port list
    input  logic [31:0] a,              // augend / minuend
    input  logic [31:0] b,              // addend / subtrahend
    output logic        exc_invalid,
    output logic        exc_divzero,    // never set for add/sub
    output logic        exc_overflow,
    output logic        exc_underflow,
    output logic        exc_inexact,
    output logic [31:0] y               // a + b  (add)  /  a - b  (sub)
);
```

### FP32 Multiplier (`fp32_mul_comb`)
```systemverilog
module fp32_mul_comb (
    input  logic [31:0] a,              // multiplicand (IEEE-754 FP32)
    input  logic [31:0] b,              // multiplier   (IEEE-754 FP32)
    output logic        exc_invalid,    // invalid operation (inf * 0, sNaN, ...)
    output logic        exc_divzero,    // divide-by-zero (never set for mul)
    output logic        exc_overflow,   // overflow flag
    output logic        exc_underflow,  // underflow flag
    output logic        exc_inexact,    // inexact result flag
    output logic [31:0] y               // product a * b (IEEE-754 FP32)
);
```

### FP32 Comparator (`fp32_cmp_comb`)
```systemverilog
module fp32_cmp_comb (
    input  logic [31:0] a,
    input  logic [31:0] b,
    input  logic        zctr,       // 0: z0=min/z1=max, 1: z0=max/z1=min
    output logic        aeqb,
    output logic        altb,
    output logic        agtb,
    output logic        unordered,
    output logic [31:0] z0,
    output logic [31:0] z1,
    output logic [ 7:0] status0,
    output logic [ 7:0] status1
);
```

`+0` and `-0` compare equal. Any NaN makes the relation unordered and passes
`a` to `z0` and `b` to `z1`. For each ordered result, status bits `[0]`, `[1]`,
and `[2]` identify zero, infinity, and NaN; bit `[7]` indicates that the output
was selected from operand `a`.

### FP32 Divider (`fp32_div_comb`)
```systemverilog
module fp32_div_comb (
    input  logic [31:0] a,              // Dividend (IEEE-754 FP32)
    input  logic [31:0] b,              // Divisor (IEEE-754 FP32)
    output logic        exc_invalid,    // Invalid operation flag
    output logic        exc_divzero,    // Divide by zero flag
    output logic        exc_overflow,   // Overflow flag
    output logic        exc_underflow,  // Underflow flag
    output logic        exc_inexact,    // Inexact result flag
    output logic [31:0] y               // Result a/b (IEEE-754 FP32)
);
```

### FP32 Square Root (`fp32_sqrt_comb`)
```systemverilog
module fp32_sqrt_comb (
    input  logic [31:0] a,              // Input (IEEE-754 FP32)
    output logic        exc_invalid,    // Invalid operation flag (sqrt of negative)
    output logic        exc_inexact,    // Inexact result flag
    output logic [31:0] y               // Result sqrt(a) (IEEE-754 FP32)
);
```

## Implementation Details

- **Algorithms**:
  - *Add/subtract* (`fp32_addsub`): magnitude ordering, right-shift-jam alignment, single-adder
    magnitude add/subtract, leading-zero normalisation bounded by the minimum exponent
  - *Multiply* (`fp32_mul_comb`): normalised 24x24-bit significand product into 48 bits, binade
    select, then the same generic normalisation and GRS rounding stage
  - *Compare* (`fp32_cmp_comb`): sign-aware magnitude comparison with IEEE zero/NaN handling and
    selectable min/max routing
  - *Divide* (`fp32_div_comb`): restoring division
  - *Square root* (`fp32_sqrt_comb`): radix-4 pair-bit method
- **Precision**: Full 24-bit significand with explicit guard/round/sticky bits
- **Special Cases**: Complete handling of ±0, ±∞, NaN, subnormals per IEEE-754
- **Rounding**: Round-to-nearest-even (ties to even, RNE) only
- **Underflow**: Gradual (subnormal results); the flag is raised only when the result is tiny
  (detected before rounding) *and* inexact
- **Exception Flags**: Full IEEE-754 exception flag generation

### IEEE-754 Implementation-Defined Behavior

IEEE-754 leaves certain behaviors as implementation-defined. This project follows
the **RISC-V** specification for these choices:

| Item | Behavior | IEEE-754 Reference |
|------|----------|--------------------|
| **Default NaN** | Canonical positive quiet NaN `0x7FC00000` | §6.2.3 — sign of default NaN is not specified |
| **NaN propagation** | Always returns canonical NaN; input payload is not preserved | §6.2.3 — propagation rules are implementation-defined |
| **NaN signaling** | Any signaling NaN input raises the invalid-operation exception | §7.2 |
| **Rounding mode** | Round-to-nearest-even (RNE) only | §4.3.1 |
| **Tininess detection** | Before rounding | §7.5 — may be detected before or after rounding |

The reference model used **only for verification** is [Berkeley SoftFloat](http://www.jhauser.us/arithmetic/SoftFloat.html)
built with `SPECIALIZE_TYPE = RISCV` (see `softfloat/build/Linux-x86_64-GCC/Makefile`).
Changing the specialization (e.g., to `8086-SSE` or `ARM-VFPv2`) will change
default-NaN encoding and propagation rules, causing test mismatches.

## Verification Strategy

The testbenches employ a multi-layered verification approach:

1. **Corner Case Testing**: Systematic testing of boundary conditions
   - All special value combinations (±0, ±∞, NaN)
   - Subnormal boundaries and gradual underflow
   - Overflow boundaries
   - Perfect squares and exact results
   - Rounding tie cases

2. **Random Testing**: Millions of pseudo-random input combinations
   - Uniform distribution across all possible FP32 values
   - Statistical coverage of rare cases
   - Long-running stress testing

3. **Reference Comparison**: Bit-exact comparison with Berkeley SoftFloat
   - Result values must match exactly (0 ULP difference)
   - Exception flags must match exactly
   - Comprehensive flag verification for all IEEE-754 conditions

## License

This project is released under the **MIT License**. See the `LICENSE` file for details.

## Revision History

See [CHANGELOG.md](CHANGELOG.md) for the full revision history.
