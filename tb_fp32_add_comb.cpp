/*
 * MIT License
 *
 * Copyright (c) 2025 adachi6k
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 */

/**
 * @file    tb_fp32_add_comb.cpp
 * @brief   Comprehensive testbench for the IEEE-754 FP32 combinational adder
 * @author  adachi6k
 * @date    2025
 *
 * @description
 * Self-checking testbench that validates the fp32_add_comb SystemVerilog module
 * against the Berkeley SoftFloat reference (f32_add).  The comparison is
 * bit-exact for both the 32-bit result (including the sign of zero and the
 * canonical NaN payload) and all five IEEE-754 exception flags.  Coverage is:
 *   - Deterministic corner cases for every IEEE-754 special value
 *   - Systematic boundary sweeps (subnormal region, near 1.0, cancellation)
 *   - Stratified random testing across the entire FP32 space
 *
 * @usage
 * ./obj_dir/Vfp32_add_comb [-v|--verbose] [--tests N] [--seed S] [N]
 *   -v, --verbose        Verbose output for every test case
 *   --tests N            Number of random vectors to run
 *   --seed S             Seed for randomized vector generation
 *   N                    Positive integer: number of random vectors to run
 *   Environment variable FP32_NUM_TESTS overrides the random-vector count.
 *   Environment variable FP32_SEED overrides the random seed.
 *
 * Any mismatch prints a detailed report and returns a non-zero exit status.
 */

#include "Vfp32_add_comb.h"
#include <cerrno>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <random>
#include <verilated.h>
// SoftFloat reference library
extern "C" {
#include "softfloat.h"
}

namespace TestConfig {
// Default number of stratified random vectors (override with FP32_NUM_TESTS
// environment variable or a positive integer command-line argument).
static constexpr long long DEFAULT_STRATIFIED_TESTS = 2000000;
static constexpr uint32_t SYSTEMATIC_SUBNORM_STEP = 0x00001111;
static constexpr uint32_t BOUNDARY_TEST_RANGE = 0x10000;

static constexpr int WEIGHT_SUBNORMALS = 10;
static constexpr int WEIGHT_SMALL_NORMALS = 8;
static constexpr int WEIGHT_MEDIUM_NORMALS = 5;
static constexpr int WEIGHT_NEAR_ONE = 15;
static constexpr int WEIGHT_LARGE_NORMALS = 8;
static constexpr int WEIGHT_NEAR_OVERFLOW = 10;
static constexpr int WEIGHT_SPECIAL_VALUES = 12;
} // namespace TestConfig

int main(int argc, char **argv) {
  bool verbose = false;
  uint64_t total_tests = TestConfig::DEFAULT_STRATIFIED_TESTS;
  uint64_t seed = 1;

  auto parse_uint64 = [](const char *s, uint64_t &out) -> bool {
    if (!s || !*s || *s == '-') return false;
    char *end = nullptr;
    errno = 0;
    unsigned long long v = std::strtoull(s, &end, 10);
    if (errno || end == s || *end != '\0') return false;
    out = static_cast<uint64_t>(v);
    return true;
  };

  if (const char *env = std::getenv("FP32_NUM_TESTS"); env && *env) {
    uint64_t v = 0;
    if (!parse_uint64(env, v)) {
      std::cerr << "Error: FP32_NUM_TESTS=\"" << env << "\" is not a valid non-negative integer\n";
      return 1;
    }
    total_tests = v;
  }
  if (const char *env = std::getenv("FP32_SEED"); env && *env) {
    uint64_t v = 0;
    if (!parse_uint64(env, v)) {
      std::cerr << "Error: FP32_SEED=\"" << env << "\" is not a valid non-negative integer\n";
      return 1;
    }
    seed = v;
  }
  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--verbose") == 0) {
      verbose = true;
    } else if (strcmp(argv[i], "--tests") == 0) {
      if (i + 1 >= argc) {
        std::cerr << "Error: --tests requires a value\nUsage: " << argv[0]
                  << " [-v] [--tests N] [--seed S] [N]\n";
        return 1;
      }
      uint64_t v = 0;
      if (!parse_uint64(argv[++i], v)) {
        std::cerr << "Error: --tests value \"" << argv[i] << "\" is not a valid non-negative integer\n";
        return 1;
      }
      total_tests = v;
    } else if (strcmp(argv[i], "--seed") == 0) {
      if (i + 1 >= argc) {
        std::cerr << "Error: --seed requires a value\nUsage: " << argv[0]
                  << " [-v] [--tests N] [--seed S] [N]\n";
        return 1;
      }
      uint64_t v = 0;
      if (!parse_uint64(argv[++i], v)) {
        std::cerr << "Error: --seed value \"" << argv[i] << "\" is not a valid non-negative integer\n";
        return 1;
      }
      seed = v;
    } else if (argv[i][0] != '-') {
      uint64_t v = 0;
      if (!parse_uint64(argv[i], v)) {
        std::cerr << "Error: positional argument \"" << argv[i] << "\" is not a valid non-negative integer\n";
        return 1;
      }
      total_tests = v;
    } else {
      std::cerr << "Error: unknown option \"" << argv[i] << "\"\nUsage: " << argv[0]
                << " [-v] [--tests N] [--seed S] [N]\n";
      return 1;
    }
  }

  std::cout << "=== IEEE-754 FP32 Combinational Adder Test Suite ===" << std::endl;
  std::cout << "Random test vectors: " << total_tests << std::endl;
  std::cout << "Random seed: " << seed << std::endl;
  std::cout << "Verbose mode: " << (verbose ? "ON" : "OFF") << std::endl;
  std::cout << "====================================================" << std::endl;

  Verilated::commandArgs(argc, argv);
  softfloat_roundingMode = softfloat_round_near_even;
  softfloat_detectTininess = softfloat_tininess_beforeRounding;
  softfloat_exceptionFlags = 0;
  Vfp32_add_comb *dut = new Vfp32_add_comb();

  int num_cc = 0;
  long long systematic_tests = 0;

  struct TestRegion {
    uint32_t start, end;
    const char *name;
    int weight;
  };

  TestRegion regions[] = {
      {0x00000000, 0x00800000, "subnormals", TestConfig::WEIGHT_SUBNORMALS},
      {0x00800000, 0x34000000, "small_normals", TestConfig::WEIGHT_SMALL_NORMALS},
      {0x34000000, 0x3f000000, "medium_normals", TestConfig::WEIGHT_MEDIUM_NORMALS},
      {0x3f000000, 0x40800000, "near_one", TestConfig::WEIGHT_NEAR_ONE},
      {0x40800000, 0x7f000000, "large_normals", TestConfig::WEIGHT_LARGE_NORMALS},
      {0x7f000000, 0x7f800000, "near_overflow", TestConfig::WEIGHT_NEAR_OVERFLOW},
      {0x7f800000, 0x7fffffff, "special_values", TestConfig::WEIGHT_SPECIAL_VALUES},
      {0x80000000, 0x80800000, "neg_subnormals", TestConfig::WEIGHT_SUBNORMALS},
      {0x80800000, 0xb4000000, "neg_small_normals", TestConfig::WEIGHT_SMALL_NORMALS},
      {0xb4000000, 0xbf000000, "neg_medium_normals", TestConfig::WEIGHT_MEDIUM_NORMALS},
      {0xbf000000, 0xc0800000, "neg_near_one", TestConfig::WEIGHT_NEAR_ONE},
      {0xc0800000, 0xff000000, "neg_large_normals", TestConfig::WEIGHT_LARGE_NORMALS},
      {0xff000000, 0xff800000, "neg_near_overflow", TestConfig::WEIGHT_NEAR_OVERFLOW},
      {0xff800000, 0xffffffff, "neg_special_values", TestConfig::WEIGHT_SPECIAL_VALUES}};

  int total_weight = 0;
  for (auto &region : regions)
    total_weight += region.weight;

  // === Exact comparison against SoftFloat f32_add ===
  auto compare_with_softfloat = [&](uint32_t a_bits, uint32_t b_bits,
                                    const char *test_name = "",
                                    bool always_verbose = false) -> bool {
    dut->a = a_bits;
    dut->b = b_bits;
    dut->eval();

    union {
      uint32_t u;
      float f;
    } rtl_result;
    rtl_result.u = dut->y;
    uint8_t rtl_flags = (dut->exc_invalid << 4) | (dut->exc_divzero << 3) |
                        (dut->exc_overflow << 2) | (dut->exc_underflow << 1) |
                        (dut->exc_inexact);

    softfloat_exceptionFlags = 0;
    float32_t a_sf, b_sf;
    a_sf.v = a_bits;
    b_sf.v = b_bits;
    float32_t ref = f32_add(a_sf, b_sf);
    uint8_t ref_flags = softfloat_exceptionFlags;

    // Strict bit-exact result match (signed zero and NaN payload included).
    bool result_match = (rtl_result.u == ref.v);
    bool flags_match = (rtl_flags == ref_flags);
    bool overall_pass = result_match && flags_match;

    if (!overall_pass || always_verbose) {
      union {
        uint32_t u;
        float f;
      } a_conv, b_conv, ref_conv;
      a_conv.u = a_bits;
      b_conv.u = b_bits;
      ref_conv.u = ref.v;
      std::cout << (strlen(test_name) > 0 ? std::string("[") + test_name + "] " : "")
                << "seed=" << seed << " "
                << "a=" << a_conv.f << "(0x" << std::hex << std::setw(8) << std::setfill('0') << a_bits << ") "
                << "b=" << b_conv.f << "(0x" << std::setw(8) << std::setfill('0') << b_bits << ") "
                << std::dec << "RTL=" << rtl_result.f << "(0x" << std::hex << std::setw(8) << std::setfill('0') << rtl_result.u << ") "
                << "Ref=" << ref_conv.f << "(0x" << std::setw(8) << std::setfill('0') << ref.v << ") "
                << (result_match ? "RES=PASS" : "RES=FAIL")
                << " |FLAG=" << (flags_match ? "PASS" : "FAIL")
                << " RTL_flags=0x" << std::hex << (int)rtl_flags
                << " Ref_flags=0x" << (int)ref_flags << std::dec << std::endl;
    }
    return overall_pass;
  };

  // === Corner cases (SoftFloat is the oracle; comments are informative) ===
  {
    static const struct {
      uint32_t a, b;
    } corner_cases[] = {
        // Signed zero
        {0x00000000, 0x00000000}, // +0 + +0 = +0
        {0x80000000, 0x80000000}, // -0 + -0 = -0
        {0x00000000, 0x80000000}, // +0 + -0 = +0 (RNE)
        {0x80000000, 0x00000000}, // -0 + +0 = +0 (RNE)
        {0x3f800000, 0x00000000}, // 1 + +0 = 1
        {0x3f800000, 0x80000000}, // 1 + -0 = 1
        {0x00000000, 0x3f800000}, // +0 + 1 = 1
        // Cancellation -> signed zero
        {0x3f800000, 0xbf800000}, // 1 + (-1) = +0
        {0xbf800000, 0x3f800000}, // -1 + 1 = +0
        {0x40490fdb, 0xc0490fdb}, // pi + (-pi) = +0
        {0x7f7fffff, 0xff7fffff}, // maxfinite + (-maxfinite) = +0
        // Infinities
        {0x7f800000, 0x7f800000}, // +inf + +inf = +inf
        {0xff800000, 0xff800000}, // -inf + -inf = -inf
        {0x7f800000, 0xff800000}, // +inf + -inf = NaN (invalid)
        {0xff800000, 0x7f800000}, // -inf + +inf = NaN (invalid)
        {0x7f800000, 0x3f800000}, // +inf + 1 = +inf
        {0xff800000, 0x3f800000}, // -inf + 1 = -inf
        {0x3f800000, 0x7f800000}, // 1 + +inf = +inf
        {0x7f800000, 0x00000000}, // +inf + 0 = +inf
        // NaN handling
        {0x7fc00000, 0x3f800000}, // qNaN + 1 = qNaN
        {0x3f800000, 0x7fc00000}, // 1 + qNaN = qNaN
        {0x7fa00000, 0x3f800000}, // sNaN + 1 = qNaN (invalid)
        {0x3f800000, 0x7fa00000}, // 1 + sNaN = qNaN (invalid)
        {0x7fa00000, 0x7fc00000}, // sNaN + qNaN = qNaN (invalid)
        {0x7f800001, 0x00000000}, // sNaN(inf payload) + 0 = qNaN (invalid)
        {0xffc00000, 0x7fc00000}, // -qNaN + qNaN = qNaN
        // Overflow
        {0x7f7fffff, 0x7f7fffff}, // maxfinite + maxfinite = +inf (overflow,inexact)
        {0xff7fffff, 0xff7fffff}, // -maxfinite + -maxfinite = -inf
        {0x7f7fffff, 0x73000000}, // maxfinite + big = +inf (overflow)
        {0x7f7fffff, 0x70000000}, // maxfinite + smaller (rounding at the top)
        // Subnormal boundaries / gradual underflow
        {0x00000001, 0x00000001}, // minsubnormal + minsubnormal = 2 ulp (exact)
        {0x007fffff, 0x00000001}, // maxsubnormal + minsubnormal = minnormal (exact)
        {0x00800000, 0x80000001}, // minnormal + (-minsubnormal) = maxsubnormal
        {0x00800000, 0x00800000}, // minnormal + minnormal (exact)
        {0x007fffff, 0x007fffff}, // maxsubnormal + maxsubnormal
        {0x00000001, 0x80000001}, // minsubnormal + (-minsubnormal) = +0
        {0x00400000, 0x00400000}, // subnormal + subnormal = minnormal
        // Round / tie-to-even near unit in the last place
        {0x3f800000, 0x33800000}, // 1 + 2^-24 -> tie, rounds to 1 (even)
        {0x3f800000, 0x34000000}, // 1 + 2^-23 -> 1 + 1ulp (exact)
        {0x3f800000, 0x33000000}, // 1 + 2^-25 -> inexact, rounds down
        {0x3f800001, 0x33800000}, // (1+1ulp) + 2^-24 -> tie up (odd->even)
        {0x40000000, 0x33800000}, // 2 + tiny -> inexact
        {0x4b000000, 0x3f800000}, // 2^23 + 1 = exact
        {0x4b800000, 0x3f800000}, // 2^24 + 1 -> tie, rounds to even
        {0x4c000000, 0x3f800000}, // 2^25 + 1 -> rounds away/inexact
        // Large exponent difference (small operand fully shifted out)
        {0x7f000000, 0x00000001}, // large + minsubnormal -> large (inexact)
        {0x00000001, 0x7f000000}, // minsubnormal + large -> large (inexact)
        {0x4f800000, 0x00800000}, // big + minnormal -> inexact
        // Mixed sign, near cancellation
        {0x40000000, 0xbf800000}, // 2 + (-1) = 1
        {0x3fc00000, 0xbf800000}, // 1.5 + (-1) = 0.5
        {0x40800000, 0xc0000000}, // 4 + (-2) = 2
        {0x3f800001, 0xbf800000}, // (1+1ulp) + (-1) = 1ulp (subnormal-ish exact)
        {0x00800001, 0x80800000}, // (minnormal+1ulp) - minnormal = minsubnormal
        // Previously exercised random-looking values
        {0x4e8a2f1b, 0xce8a2f10}, // near cancellation, large magnitude
        {0x3e000000, 0x3e000000}, // 0.125 + 0.125 = 0.25 (exact)
        {0xbf000000, 0x3e800000}, // -0.5 + 0.25 = -0.25
    };
    num_cc = sizeof(corner_cases) / sizeof(corner_cases[0]);
    for (int i = 0; i < num_cc; ++i) {
      if (!compare_with_softfloat(corner_cases[i].a, corner_cases[i].b,
                                  ("CASE " + std::to_string(i)).c_str(), verbose)) {
        std::cout << "[CASE " << i << "] FAILED" << std::endl;
        compare_with_softfloat(corner_cases[i].a, corner_cases[i].b,
                               ("CASE " + std::to_string(i)).c_str(), true);
        dut->final();
        delete dut;
        return 1;
      }
    }
    std::cout << "=== Corner-case tests done (" << num_cc << ") ===" << std::endl;
  }

  // === Systematic boundary sweeps ===
  std::cout << "=== Systematic boundary testing ===" << std::endl;

  // Subnormal operand a against representative b values.
  {
    uint32_t bs[] = {0x00000001, 0x007fffff, 0x00800000, 0x3f800000, 0xbf800000,
                     0x40000000, 0x7f000000, 0x00000000, 0x80000000, 0x7f800000};
    for (uint32_t sub = 0x00000001; sub <= 0x007fffff;
         sub += TestConfig::SYSTEMATIC_SUBNORM_STEP) {
      for (uint32_t b : bs) {
        if (!compare_with_softfloat(
                sub, b,
                ("SYS_SUBNORM:" + std::to_string(systematic_tests)).c_str())) {
          dut->final();
          delete dut;
          return 1;
        }
        systematic_tests++;
      }
    }
  }

  // Boundary transitions around 1.0 (both operands).
  for (uint32_t i = 0; i < TestConfig::BOUNDARY_TEST_RANGE; ++i) {
    uint32_t a = 0x3f800000 + i - 0x8000;
    uint32_t b = 0x3f800000 + (i * 17) - 0x8000;
    if (!compare_with_softfloat(
            a, b, ("BOUNDARY:" + std::to_string(systematic_tests)).c_str())) {
      dut->final();
      delete dut;
      return 1;
    }
    systematic_tests++;
  }

  // Massive-cancellation sweep: a and a value very close to -a.
  for (uint32_t i = 0; i < TestConfig::BOUNDARY_TEST_RANGE; ++i) {
    uint32_t a = 0x40490fdb + i - 0x8000; // around pi
    uint32_t b = (a ^ 0x80000000) + (i & 0x3f) - 0x20; // near -a with jitter
    if (!compare_with_softfloat(
            a, b, ("CANCEL:" + std::to_string(systematic_tests)).c_str())) {
      dut->final();
      delete dut;
      return 1;
    }
    systematic_tests++;
  }

  std::cout << "Systematic tests completed: " << systematic_tests << std::endl;

  // === Stratified random testing ===
  std::cout << "=== Stratified random testing ===" << std::endl;
  std::mt19937_64 gen1(seed);
  std::mt19937_64 gen2(seed ^ 0x9e3779b97f4a7c15ULL);
  std::mt19937_64 gen3(seed ^ 0xc2b2ae3d27d4eb4fULL);
  std::uniform_int_distribution<uint32_t> dis(0, 0xFFFFFFFF);

  for (uint64_t t = 0; t < total_tests; ++t) {
    int region_select = dis(gen1) % total_weight;
    int current_weight = 0;
    TestRegion *sel = nullptr;
    for (auto &region : regions) {
      current_weight += region.weight;
      if (region_select < current_weight) {
        sel = &region;
        break;
      }
    }
    if (!sel)
      sel = &regions[0];

    uint32_t a_bits, b_bits;
    uint32_t range_a = sel->end - sel->start;
    a_bits = range_a ? sel->start + (dis(gen1) % range_a) : sel->start;
    if (t % 3 == 0) {
      uint32_t range_b = sel->end - sel->start;
      b_bits = range_b ? sel->start + (dis(gen2) % range_b) : sel->start;
    } else if (t % 3 == 1) {
      // Bias towards additive cancellation: b close to -a.
      b_bits = (a_bits ^ 0x80000000) ^ (dis(gen2) & 0x7);
    } else {
      b_bits = dis(gen3);
    }

    if (!compare_with_softfloat(a_bits, b_bits,
                                ("Rand:" + std::to_string(t)).c_str())) {
      dut->final();
      delete dut;
      return 1;
    }
  }

  std::cout << "\n=== Test Coverage Summary ===" << std::endl;
  std::cout << "Corner cases: " << num_cc << std::endl;
  std::cout << "Systematic tests: " << systematic_tests << std::endl;
  std::cout << "Random tests: " << total_tests << std::endl;
  std::cout << "Total: " << (num_cc + systematic_tests + total_tests) << std::endl;
  std::cout << "ALL TESTS PASSED" << std::endl;

  dut->final();
  delete dut;
  return 0;
}
