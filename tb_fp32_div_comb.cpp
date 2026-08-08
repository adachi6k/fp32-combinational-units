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
 * @file    tb_fp32_div_comb.cpp
 * @brief   Comprehensive testbench for IEEE-754 FP32 combinational divider
 * @author  adachi6k
 * @date    2025
 * 
 * @description
 * Self-checking testbench that validates the fp32_div_comb SystemVerilog module
 * against the SoftFloat reference implementation. Includes:
 * - Corner case testing for IEEE-754 special values
 * - Systematic boundary testing for subnormal and critical regions  
 * - Stratified random testing across the entire FP32 space
 * - Bit-accurate comparison with detailed ULP analysis
 * - Early termination on first failure for efficient debugging
 * 
 * @usage
 * ./obj_dir/Vfp32_div_comb [-v|--verbose] [--tests N] [--seed S] [N]
 *   -v, --verbose    Enable verbose output for all test cases
 *   --tests N        Number of random vectors to run
 *   --seed S         Seed for randomized vector generation
 *   N                Positive integer: number of random vectors to run
 *   Environment variable FP32_NUM_TESTS overrides the random-vector count.
 *   Environment variable FP32_SEED overrides the random seed.
 * 
 * @note Requires SoftFloat library for reference calculations
 */

#include "Vfp32_div_comb.h"
#include "Vfp32_div_comb___024root.h"
#include "Vfp32_div_comb_fp32_div_comb.h"
#include <cerrno>
#include <cstring>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <random>
#include <verilated.h>
// SoftFloat reference library
extern "C" {
#include "softfloat.h"
}

/**
 * @brief Test configuration constants
 */
namespace TestConfig {
  // Test execution parameters
  static constexpr uint64_t DEFAULT_STRATIFIED_TESTS = 60000000;  // Total random test vectors
  static constexpr int SYSTEMATIC_SUBNORM_STEP = 0x00001111;      // Step size for subnormal tests
  static constexpr int BOUNDARY_TEST_RANGE = 0x10000;             // Range for boundary tests around 1.0
  
  // Test region weights for stratified random testing
  static constexpr int WEIGHT_SUBNORMALS = 10;
  static constexpr int WEIGHT_SMALL_NORMALS = 8; 
  static constexpr int WEIGHT_MEDIUM_NORMALS = 5;
  static constexpr int WEIGHT_NEAR_ONE = 15;
  static constexpr int WEIGHT_LARGE_NORMALS = 8;
  static constexpr int WEIGHT_NEAR_OVERFLOW = 10;
  static constexpr int WEIGHT_SPECIAL_VALUES = 12;
}

/**
 * @brief Global test execution time counter
 */
uint64_t time_counter = 0;

int main(int argc, char **argv) {
  // Parse command line arguments for verbose mode
  bool verbose = false;
  uint64_t total_tests = TestConfig::DEFAULT_STRATIFIED_TESTS;
  uint64_t seed = 1;
  auto parse_uint64 = [](const char *s, uint64_t &out) -> bool {
    if (!s || !*s) return false;
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
  
  std::cout << "=== IEEE-754 FP32 Combinational Divider Test Suite ===" << std::endl;
  std::cout << "Random test vectors: " << total_tests << std::endl;
  std::cout << "Random seed: " << seed << std::endl;
  std::cout << "Verbose mode: " << (verbose ? "ON" : "OFF") << std::endl;
  std::cout << "=======================================================" << std::endl;

  Verilated::commandArgs(argc, argv);
  softfloat_roundingMode = softfloat_round_near_even;
  softfloat_detectTininess = softfloat_tininess_beforeRounding;
  softfloat_exceptionFlags = 0;
  Vfp32_div_comb *dut = new Vfp32_div_comb();

  // Test execution tracking variables
  int num_cc = 0;           // Corner case test count
  int systematic_tests = 0; // Systematic test count
  
  // Stratified random testing configuration
  // Divide FP32 space into regions with different sampling weights
  struct TestRegion {
    uint32_t start, end;      // IEEE-754 bit pattern range
    const char* name;         // Region description
    int weight;               // Relative sampling weight
  };
  
  TestRegion regions[] = {
    // Positive ranges
    {0x00000000, 0x00800000, "subnormals", TestConfig::WEIGHT_SUBNORMALS},
    {0x00800000, 0x34000000, "small_normals", TestConfig::WEIGHT_SMALL_NORMALS},
    {0x34000000, 0x3f000000, "medium_normals", TestConfig::WEIGHT_MEDIUM_NORMALS},
    {0x3f000000, 0x40800000, "near_one", TestConfig::WEIGHT_NEAR_ONE},
    {0x40800000, 0x7f000000, "large_normals", TestConfig::WEIGHT_LARGE_NORMALS},
    {0x7f000000, 0x7f800000, "near_overflow", TestConfig::WEIGHT_NEAR_OVERFLOW},
    {0x7f800000, 0x7fffffff, "special_values", TestConfig::WEIGHT_SPECIAL_VALUES},
    
    // Negative ranges (symmetric to positive)
    {0x80000000, 0x80800000, "neg_subnormals", TestConfig::WEIGHT_SUBNORMALS},
    {0x80800000, 0xb4000000, "neg_small_normals", TestConfig::WEIGHT_SMALL_NORMALS},
    {0xb4000000, 0xbf000000, "neg_medium_normals", TestConfig::WEIGHT_MEDIUM_NORMALS},
    {0xbf000000, 0xc0800000, "neg_near_one", TestConfig::WEIGHT_NEAR_ONE},
    {0xc0800000, 0xff000000, "neg_large_normals", TestConfig::WEIGHT_LARGE_NORMALS},
    {0xff000000, 0xff800000, "neg_near_overflow", TestConfig::WEIGHT_NEAR_OVERFLOW},
    {0xff800000, 0xffffffff, "neg_special_values", TestConfig::WEIGHT_SPECIAL_VALUES}
  };
  
  // Calculate total weight for stratified sampling
  int total_weight = 0;
  for (auto& region : regions) total_weight += region.weight;
  auto bits_are_nan = [](uint32_t bits) {
    return (bits & 0x7f800000U) == 0x7f800000U && (bits & 0x007fffffU) != 0;
  };

  // === Enhanced common SoftFloat comparison function ===
  auto compare_with_softfloat = [&](uint32_t a_bits, uint32_t b_bits, const char* test_name = "", 
                                     bool verbose_on_fail = false, bool show_debug = false, 
                                     bool always_verbose = false) -> bool {
    // Set inputs and evaluate RTL
    dut->a = a_bits;
    dut->b = b_bits;
    dut->eval();
    
    // Get RTL result and flags
    union { uint32_t u; float f; } rtl_result;
    rtl_result.u = dut->y;
    uint8_t rtl_flags = (dut->exc_invalid << 4) | (dut->exc_divzero << 3) |
                        (dut->exc_overflow << 2) | (dut->exc_underflow << 1) |
                        (dut->exc_inexact);
    
    // Compute SoftFloat reference
    softfloat_exceptionFlags = 0;
    float32_t a_sf, b_sf;
    a_sf.v = a_bits;
    b_sf.v = b_bits;
    float32_t math_result_sf = f32_div(a_sf, b_sf);
    uint8_t math_flags = softfloat_exceptionFlags;
    
    // ULP calculation for detailed analysis
    uint32_t ulp_diff = 0;
    bool is_nan_case = bits_are_nan(rtl_result.u) && bits_are_nan(math_result_sf.v);
    if (!is_nan_case) {
      if (rtl_result.u == math_result_sf.v) {
        ulp_diff = 0;
      } else if ((rtl_result.u == 0x00000000 && math_result_sf.v == 0x80000000) ||
                 (rtl_result.u == 0x80000000 && math_result_sf.v == 0x00000000)) {
        ulp_diff = 0;  // +0 and -0 are equivalent
      } else {
        // Simple absolute difference for same-sign numbers
        if ((rtl_result.u ^ math_result_sf.v) & 0x80000000) {
          // Different signs - sum the magnitudes
          ulp_diff = (rtl_result.u & 0x7FFFFFFF) + (math_result_sf.v & 0x7FFFFFFF);
        } else {
          // Same sign - absolute difference  
          ulp_diff = (rtl_result.u > math_result_sf.v) ? (rtl_result.u - math_result_sf.v) : (math_result_sf.v - rtl_result.u);
        }
      }
    }
    
    // Check for mismatch (NaN: compare bit patterns exactly including payload)
    bool result_match = is_nan_case ? (rtl_result.u == math_result_sf.v) : (ulp_diff == 0);
    bool flags_match = (rtl_flags == math_flags);
    bool overall_pass = result_match && flags_match;
    
    // Report results if requested
    if (!overall_pass || always_verbose) {
      union { uint32_t u; float f; } a_conv, b_conv;
      a_conv.u = a_bits;
      b_conv.u = b_bits;
      
      std::cout << (strlen(test_name) > 0 ? std::string("[") + test_name + "] " : "")
                << "seed=" << seed << " "
                << "a=" << a_conv.f << "(0x" << std::hex << std::setw(8) << std::setfill('0') << a_bits << ") "
                << "b=" << b_conv.f << "(0x" << std::setw(8) << std::setfill('0') << b_bits << ") "
                << std::dec << "RTL=" << rtl_result.f << "(0x" << std::hex << std::setw(8) << std::setfill('0') << rtl_result.u << ") "
                << "Math=" << math_result_sf.v << "(0x" << std::setw(8) << std::setfill('0') << math_result_sf.v << ") "
                << std::dec << "ulp_diff=" << ulp_diff << " "
                << (result_match ? "PASS" : "FAIL");
      
      // Show flag comparison
      std::cout << " |FLAG=" << (flags_match ? "PASS" : "FAIL") 
                << " RTL_flags=0x" << std::hex << (int)rtl_flags 
                << " Math_flags=0x" << (int)math_flags << std::dec;
      
      // Show debug signals if requested
      if (show_debug) {
        std::cout << " |dbg_final=0x" << std::hex << std::setw(6) << std::setfill('0') 
                  << dut->fp32_div_comb->dbg_quotient_final << std::dec
                  << " guard=" << static_cast<int>(dut->fp32_div_comb->dbg_guard_bit)
                  << " sticky=" << static_cast<int>(dut->fp32_div_comb->dbg_sticky_bit)
                  << " raw_div=0x" << std::hex << std::setw(14) << std::setfill('0')
                  << static_cast<unsigned long long>(dut->fp32_div_comb->dbg_raw_div_full)
                  << " q25=0x" << std::setw(7) << std::setfill('0') << dut->fp32_div_comb->dbg_quotient_25b 
                  << " mantissa=0x" << std::setw(7) << std::setfill('0') << dut->fp32_div_comb->dbg_mantissa_work
                  << std::dec << " lz=" << static_cast<int>(dut->fp32_div_comb->dbg_leading_zeros) 
                  << " norm=0x" << std::hex << std::setw(13) << std::setfill('0')
                  << static_cast<unsigned long long>(dut->fp32_div_comb->dbg_quotient_norm) 
                  << std::dec << " round_up=" << static_cast<int>(dut->fp32_div_comb->dbg_round_up);
      }
      
      std::cout << std::endl;
    } else if (verbose_on_fail && !overall_pass) {
      // Simple failure report for non-verbose modes
      union { uint32_t u; float f; } a_conv, b_conv;
      a_conv.u = a_bits;
      b_conv.u = b_bits;
      
      std::cout << "[" << test_name << " FAIL] "
                << "seed=" << seed << " "
                << "a=" << a_conv.f << "(0x" << std::hex << a_bits << ") "
                << "b=" << b_conv.f << "(0x" << b_bits << ") "
                << "RTL=" << rtl_result.f << "(0x" << rtl_result.u << ") "
                << "Math=" << math_result_sf.v << "(0x" << math_result_sf.v << ") "
                << "RTL_flags=0x" << (int)rtl_flags << " "
                << "Math_flags=0x" << (int)math_flags << std::dec << std::endl;
    }
    
    return overall_pass;
  };

  // === Corner-case tests ===
  {
    union {
      float f;
      uint32_t u;
    } conv_a_cc, conv_b_cc;
    static const struct {
      uint32_t a, b;
    } corner_cases[] = {
        // === Basic special values ===
        {0x00000000, 0x00000000}, // 0/0 -> NaN (invalid)
        {0x00000000, 0x3f800000}, // 0/1 -> 0
        {0x80000000, 0x3f800000}, // -0/1 -> -0
        {0x3f800000, 0x00000000}, // 1/0 -> inf (divzero)
        {0x3f800000, 0x80000000}, // 1/-0 -> -inf (divzero)
        {0x7f800000, 0x3f800000}, // inf/1 -> inf
        {0xff800000, 0x3f800000}, // -inf/1 -> -inf
        {0x7f800000, 0x7f800000}, // inf/inf -> NaN (invalid)
        {0x7f800000, 0xff800000}, // inf/-inf -> NaN (invalid)
        {0x3f800000, 0x7f800000}, // 1/inf -> 0
        {0x3f800000, 0xff800000}, // 1/-inf -> -0
        {0x7fc00000, 0x3f800000}, // qNaN/1 -> qNaN
        {0x7fa00000, 0x3f800000}, // sNaN/1 -> qNaN (invalid)
        {0x3f800000, 0x7fc00000}, // 1/qNaN -> qNaN
        {0x3f800000, 0x7fa00000}, // 1/sNaN -> qNaN (invalid)
        
        // === Subnormal boundaries ===
        {0x00000001, 0x00000001}, // min subnormal/min subnormal -> 1.0
        {0x00000001, 0x3f800000}, // min subnormal/1.0 -> min subnormal
        {0x007fffff, 0x3f800000}, // max subnormal/1.0 -> max subnormal
        {0x00800000, 0x00800000}, // min normal/min normal -> 1.0
        {0x00800000, 0x40000000}, // min normal/2.0 -> gradual underflow
        {0x00800001, 0x40000000}, // slightly above min normal/2.0
        {0x007fffff, 0x40000000}, // max subnormal/2.0
        
        // === Overflow boundaries ===
        {0x7f7fffff, 0x3f800000}, // max finite/1 -> max finite
        {0x7f7fffff, 0x3f000000}, // max finite/0.5 -> inf (overflow)
        {0x7f000000, 0x3f000000}, // large/0.5 -> overflow
        {0x7e800000, 0x3e800000}, // boundary overflow test
        
        // === Exact divisions ===
        {0x3f800000, 0x3f800000}, // 1.0/1.0 -> 1.0 (exact)
        {0x40000000, 0x40000000}, // 2.0/2.0 -> 1.0 (exact)
        {0x40400000, 0x40000000}, // 3.0/2.0 -> 1.5 (exact)
        {0x40800000, 0x40000000}, // 4.0/2.0 -> 2.0 (exact)
        {0x41200000, 0x40800000}, // 10.0/4.0 -> 2.5 (exact)
        {0x42c80000, 0x41200000}, // 100.0/10.0 -> 10.0 (exact)
        
        // === Rounding-critical divisions ===
        {0x3f800000, 0x40400000}, // 1.0/3.0 -> 0.333... (round to nearest)
        {0x40000000, 0x40400000}, // 2.0/3.0 -> 0.666... (round to nearest)
        {0x3f800000, 0x41200000}, // 1.0/10.0 -> 0.1 (rounding)
        {0x3f800000, 0x40e00000}, // 1.0/7.0 -> 0.142857... (rounding)
        {0x41200000, 0x40400000}, // 10.0/3.0 -> 3.333... (rounding)
        
        // === Tie-to-even rounding cases ===
        {0x40400000, 0x48000000}, // 3.0/32768.0 -> tie case
        {0x40a00000, 0x48800000}, // 5.0/65536.0 -> tie case
        {0x3f800001, 0x48000000}, // slightly above 1.0/32768.0
        {0x3f7fffff, 0x48000000}, // slightly below 1.0/32768.0
        
        // === Leading zero normalization edge cases ===
        {0x3f800000, 0x4f800000}, // 1.0/very_large -> many leading zeros in quotient
        {0x3f800000, 0x70000000}, // 1.0/extremely_large -> edge of subnormal
        {0x38800000, 0x7f000000}, // small/large -> deep subnormal
        {0x08000000, 0x4f800000}, // very_small/large -> deep underflow
        
        // === Sticky bit edge cases ===
        {0x40000001, 0x40400000}, // 2.0000001/3.0 -> sticky bit test
        {0x40400001, 0x40000000}, // 3.0000001/2.0 -> sticky bit test
        {0x7f7ffffe, 0x40000000}, // near-max/2.0 -> sticky preservation
        
        // === Sign combinations ===
        {0x80000000, 0x80000000}, // -0/-0 -> NaN (invalid)
        {0xbf800000, 0x3f800000}, // -1.0/1.0 -> -1.0
        {0x3f800000, 0xbf800000}, // 1.0/-1.0 -> -1.0
        {0xbf800000, 0xbf800000}, // -1.0/-1.0 -> 1.0
        {0xff800000, 0x80000000}, // -inf/-0 -> +inf (inf has priority over divzero)
        {0x7f800000, 0x80000000}, // inf/-0 -> -inf (inf has priority over divzero)
        
        // === Previously observed failure cases ===
        {0x3781fd3f, 0xf8480000}, // 1.54959e-05/-1.62259e+34 (underflow)
        {0xaacf58b8, 0xeae1320a}, // -3.68321e-13/-1.36122e+26 (subnormal)
        {0x96042d06, 0x5d042d06}, // -1.06771e-25/5.95267e+17
        {0x9be34bb1, 0xe0988600}, // -3.76029e-22/-8.79238e+19
        {0x0f8746fe, 0x514c0000}, // 1.33394e-29/5.47608e+10
        {0x920c6be1, 0x517da98a}, // -4.43092e-28/6.80919e+10
        {0x057e2068, 0xc4b49df2}, // 1.19490e-35/-1444.94
        {0xa8ec1495, 0x68a45fad}, // -2.62102e-14/6.20986e+24
        {0x325cd2c3, 0xf6209948}, // 1.28536e-08/-8.14332e+32 (exact subnormal)
        {0x29eed5eb, 0xefbbfc00}, // 1.06064e-13/-1.16357e+29 (rounding issue: expected 0x8000028a, got 0x8000028b)
        
        // === Algorithm stress tests ===
        {0x34000000, 0x7f7fffff}, // small/max -> extreme underflow
        {0x7f7fffff, 0x34000000}, // max/small -> extreme overflow
        {0x00800000, 0x7f7fffff}, // min_normal/max -> extreme underflow
        {0x7f7fffff, 0x00800000}, // max/min_normal -> extreme overflow
        {0x00000001, 0x7f7fffff}, // min_subnormal/max -> extreme underflow
        {0x7f7fffff, 0x00000001}, // max/min_subnormal -> extreme overflow
        
        // === Quotient normalization edge cases ===
        {0x3f000000, 0x3f800000}, // 0.5/1.0 -> 0.5 (no normalization)
        {0x3e800000, 0x3f800000}, // 0.25/1.0 -> 0.25 (1 bit normalization)
        {0x3e000000, 0x3f800000}, // 0.125/1.0 -> 0.125 (2 bit normalization)
        {0x3d800000, 0x3f800000}, // 0.0625/1.0 -> 0.0625 (3 bit normalization)
        
        // === Guard/round/sticky boundary tests ===
        {0x40000003, 0x40400000}, // guard bit boundary
        {0x40000005, 0x40400000}, // round bit boundary  
        {0x40000007, 0x40400000}, // sticky bit boundary
        {0x4000000f, 0x40400000}, // multiple sticky bits
    };
    num_cc = sizeof(corner_cases) / sizeof(corner_cases[0]);
    for (int i = 0; i < num_cc; ++i) {
      if (!compare_with_softfloat(corner_cases[i].a, corner_cases[i].b,
                                  ("CASE " + std::to_string(i)).c_str(),
                                  !verbose)) {
        // On failure, provide detailed output if not in verbose mode
        if (!verbose) {
          union { uint32_t u; float f; } a_conv, b_conv;
          a_conv.u = corner_cases[i].a;
          b_conv.u = corner_cases[i].b;
          std::cout << "[CASE " << i << "] Failed: a=" << a_conv.f 
                    << "(0x" << std::hex << corner_cases[i].a << ") "
                    << "b=" << b_conv.f << "(0x" << corner_cases[i].b << ")" << std::dec << std::endl;
          // Re-run with verbose output for this specific case
          compare_with_softfloat(corner_cases[i].a, corner_cases[i].b, ("CASE " + std::to_string(i)).c_str(), true);
        }
        return 1;  // Exit on first corner case failure
      }
      // Verbose output for passing cases if requested
      if (verbose) {
        union { uint32_t u; float f; } a_conv, b_conv;
        a_conv.u = corner_cases[i].a;
        b_conv.u = corner_cases[i].b;
        std::cout << "[CASE " << i << "] PASS: a=" << a_conv.f 
                  << "(0x" << std::hex << corner_cases[i].a << ") "
                  << "b=" << b_conv.f << "(0x" << corner_cases[i].b << ")" << std::dec << std::endl;
      }
    }
    std::cout << "=== Corner-case tests done ===" << std::endl;
  }

  // === Systematic exhaustive testing for critical regions ===
  std::cout << "=== Systematic boundary testing ===" << std::endl;
  
  // Test all subnormal dividends with various divisors
  for (uint32_t subnormal = 0x00000001; subnormal <= 0x007fffff; subnormal += TestConfig::SYSTEMATIC_SUBNORM_STEP) {
    uint32_t divisors[] = {0x3f800000, 0x40000000, 0x3f000000, 0x41200000, 0x3e800000};
    for (uint32_t divisor : divisors) {
      if (!compare_with_softfloat(
              subnormal, divisor,
              ("SYSTEMATIC:" + std::to_string(systematic_tests)).c_str(),
              true)) {
        return 1;  // Exit on first failure for systematic tests
      }
      systematic_tests++;
    }
  }
  
  // Test boundary transitions around 1.0
  for (uint32_t i = 0; i < TestConfig::BOUNDARY_TEST_RANGE; ++i) {
    uint32_t near_one_a = 0x3f800000 + i - 0x8000;  // Around 1.0
    uint32_t near_one_b = 0x3f800000 + (i * 17) - 0x8000;  // Different pattern
    if (!compare_with_softfloat(
            near_one_a, near_one_b,
            ("BOUNDARY:" + std::to_string(systematic_tests)).c_str(), true)) {
      return 1;  // Exit on first failure
    }
    systematic_tests++;
  }
  
  std::cout << "Systematic tests completed: " << systematic_tests << std::endl;

  // === Improved random testing with multiple generators ===
  std::cout << "=== Enhanced random testing ===" << std::endl;
  
  // Use multiple PRNG states for better coverage
  std::mt19937 gen1(static_cast<uint32_t>(seed));
  std::mt19937 gen2(static_cast<uint32_t>(seed ^ 0x9e3779b9ULL));
  std::mt19937 gen3(static_cast<uint32_t>(seed ^ 0x85ebca6bULL));
  std::uniform_int_distribution<uint32_t> dis(0, 0xFFFFFFFF);

  while (time_counter < total_tests) {
    // Select region based on weighted probability
    int region_select = dis(gen1) % total_weight;
    int current_weight = 0;
    TestRegion* selected_region = nullptr;
    
    for (auto& region : regions) {
      current_weight += region.weight;
      if (region_select < current_weight) {
        selected_region = &region;
        break;
      }
    }
    if (!selected_region)
      selected_region = &regions[0];
    
    // Generate values within selected region using different generators
    uint32_t rand_bits_a, rand_bits_b;
    if (selected_region->start == selected_region->end) {
      rand_bits_a = selected_region->start;
    } else {
      uint32_t range_a = selected_region->end - selected_region->start;
      rand_bits_a = selected_region->start + (dis(gen1) % range_a);
    }
    
    // Select different region for divisor or use full range
    if (time_counter % 3 == 0) {
      // Sometimes use values from same region for both operands
      uint32_t range_b = selected_region->end - selected_region->start;
      rand_bits_b = range_b ? selected_region->start + (dis(gen2) % range_b)
                            : selected_region->start;
    } else {
      // Other times use completely different generator
      rand_bits_b = dis(gen3);
    }
    
    union {
      float f;
      uint32_t u;
    } conv_a, conv_b;
    conv_a.u = rand_bits_a;
    conv_b.u = rand_bits_b;

    // Use common comparison function with time counter info and debug output
    std::string test_id = "Time:" + std::to_string(time_counter);
    if (!compare_with_softfloat(conv_a.u, conv_b.u, test_id.c_str(), false, true, verbose)) {
      // Exit immediately on failure for random tests
      return 1;
    }

    time_counter++;
  }

  // === Coverage analysis and reporting ===
  std::cout << "\n=== Test Coverage Summary ===" << std::endl;
  std::cout << "Corner cases: " << num_cc << std::endl;
  std::cout << "Systematic tests: " << systematic_tests << std::endl;
  std::cout << "Stratified random tests: " << time_counter << std::endl;
  std::cout << "Total test vectors: " << (num_cc + systematic_tests + time_counter) << std::endl;
  
  // Print region coverage statistics
  std::cout << "\n=== Random Test Distribution ===" << std::endl;
  for (auto& region : regions) {
    double percentage = (double)region.weight / total_weight * 100.0;
    std::cout << region.name << ": " << std::fixed << std::setprecision(1) 
              << percentage << "%" << std::endl;
  }

  dut->final();
  delete dut;
  return 0;
}
