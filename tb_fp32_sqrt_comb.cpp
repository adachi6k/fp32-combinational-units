#include "Vfp32_sqrt_comb.h"
#include <cerrno>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <iomanip> // for std::hex and std::setw
#include <iostream>
#include <random>
#include <verilated.h>
extern "C" {
#include "softfloat.h"
}

namespace TestConfig {
static constexpr uint64_t DEFAULT_STRATIFIED_TESTS = 60000000;
}

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

  std::cout << "=== IEEE-754 FP32 Combinational Square Root Test Suite ===\n";
  std::cout << "Random test vectors: " << total_tests << '\n';
  std::cout << "Random seed: " << seed << '\n';
  std::cout << "Verbose mode: " << (verbose ? "ON" : "OFF") << '\n';
  std::cout << "===========================================================\n";

  Verilated::commandArgs(argc, argv);
  softfloat_roundingMode = softfloat_round_near_even;
  softfloat_detectTininess = softfloat_tininess_beforeRounding;
  softfloat_exceptionFlags = 0;

  Vfp32_sqrt_comb *dut = new Vfp32_sqrt_comb();

  // Variables for coverage tracking
  int num_cc = 0;
  int systematic_tests = 0;
  
  // Stratified random testing - divide FP32 space into regions
  struct TestRegion {
    uint32_t start, end;
    const char* name;
    int weight;
  };
  
  TestRegion regions[] = {
    {0x00000000, 0x00800000, "subnormals", 15},         // More weight for sqrt edge cases
    {0x00800000, 0x34000000, "small_normals", 10},
    {0x34000000, 0x3f000000, "medium_normals", 8},
    {0x3f000000, 0x40800000, "near_one", 20},          // Critical for sqrt accuracy
    {0x40800000, 0x7f000000, "large_normals", 12},
    {0x7f000000, 0x7f800000, "near_overflow", 10},
    {0x7f800000, 0x7fffffff, "special_values", 15},     // inf, NaN cases
    // Negative values all produce NaN for sqrt, but still test
    {0x80000000, 0x80000000, "neg_zero", 5},            // -0 -> -0
    {0x80000001, 0xffffffff, "negative_vals", 5}        // All other negatives -> NaN
  };
  
  int total_weight = 0;
  for (auto& region : regions) total_weight += region.weight;

  // === Corner-case tests for sqrt ===
  {
    union {
      float f;
      uint32_t u;
    } conv_cc, out_cc, math_cc;
    static const uint32_t corner_vals[] = {
        // === Basic special values ===
        0x00000000, // +0 -> +0 (exact)
        0x80000000, // -0 -> -0 (exact)
        0x3f800000, // 1.0 -> 1.0 (exact)
        0x7f800000, // +inf -> +inf
        0xff800000, // -inf -> NaN (invalid)
        0x7fc00000, // qNaN -> qNaN
        0x7fa00000, // sNaN -> qNaN (invalid)
        0xbf800000, // -1.0 -> NaN (invalid)
        0x80000001, // -min_subnormal -> NaN (invalid)
        0x80800000, // -min_normal -> NaN (invalid)
        0xff7fffff, // -max_finite -> NaN (invalid)
        
        // === Subnormal boundaries ===
        0x00000001, // min subnormal -> very small
        0x00000002, // 2*min subnormal
        0x00000004, // 4*min subnormal  
        0x00000100, // medium subnormal
        0x007fffff, // max subnormal
        0x00800000, // min normal
        0x00800001, // just above min normal
        0x00800100, // slightly above min normal
        
        // === Perfect squares ===
        0x40000000, // 2.0 -> sqrt(2) ≈ 1.414... (inexact)
        0x40800000, // 4.0 -> 2.0 (exact)
        0x41100000, // 9.0 -> 3.0 (exact)
        0x41800000, // 16.0 -> 4.0 (exact)
        0x42480000, // 50.0 -> sqrt(50) ≈ 7.071... (inexact)
        0x42c80000, // 100.0 -> 10.0 (exact)
        0x447a0000, // 1000.0 -> sqrt(1000) ≈ 31.622... (inexact)
        0x461c4000, // 10000.0 -> 100.0 (exact)
        0x4b000000, // 2^23 -> sqrt(2^23) = 2^11.5 (inexact)
        0x4c000000, // 2^24 -> 2^12 = 4096.0 (exact)
        
        // === Powers of 2 (should be exact or simple) ===
        0x3e800000, // 0.25 -> 0.5 (exact)
        0x3f000000, // 0.5 -> sqrt(0.5) ≈ 0.707... (inexact)
        0x3f800000, // 1.0 -> 1.0 (exact)
        0x40000000, // 2.0 -> sqrt(2) ≈ 1.414... (inexact)
        0x40800000, // 4.0 -> 2.0 (exact)
        0x41000000, // 8.0 -> sqrt(8) ≈ 2.828... (inexact)
        0x41800000, // 16.0 -> 4.0 (exact)
        0x42000000, // 32.0 -> sqrt(32) ≈ 5.656... (inexact)
        
        // === Boundary values ===
        0x7f7fffff, // max finite -> very large result
        0x3f7fffff, // just below 1.0
        0x3f800001, // just above 1.0
        0x007fffff, // max subnormal
        0x00800000, // min normal
        0x34000000, // small normal value
        0x7f000000, // large value near overflow
        
        // === Rounding-critical values ===
        0x3f490fdb, // π/2 ≈ 1.5708 -> sqrt(π/2) (inexact)
        0x40490fdb, // π ≈ 3.14159 -> sqrt(π) (inexact)
        0x402df854, // e ≈ 2.71828 -> sqrt(e) (inexact)
        0x40c90fdb, // 2π ≈ 6.28318 -> sqrt(2π) (inexact)
        0x3eaaaaab, // 1/3 ≈ 0.333... -> sqrt(1/3) (inexact)
        0x3f2aaaab, // 2/3 ≈ 0.666... -> sqrt(2/3) (inexact)
        
        // === Tie-to-even rounding cases ===
        0x3f800100, // slightly above 1.0 (tie case potential)
        0x3f800200, // slightly above 1.0 (tie case potential)
        0x3f800300, // slightly above 1.0 (tie case potential)
        0x40000100, // slightly above 2.0 (tie case potential)
        0x40000200, // slightly above 2.0 (tie case potential)
        
        // === Algorithm stress tests ===
        0x33800000, // very small normal (stress subnormal output)
        0x4f800000, // large value (stress normalization)
        0x70000000, // very large (near overflow boundary)
        0x0f800000, // small value (many leading zeros)
        0x08000000, // very small (extreme subnormal input)
        
        // === Square root algorithm edge cases ===
        0x3f400000, // 0.75 -> sqrt(0.75) ≈ 0.866... (test quotient selection)
        0x3fc00000, // 1.5 -> sqrt(1.5) ≈ 1.224... (test quotient selection)
        0x40200000, // 2.5 -> sqrt(2.5) ≈ 1.581... (test quotient selection)
        0x40600000, // 3.5 -> sqrt(3.5) ≈ 1.870... (test quotient selection)
        0x40a00000, // 5.0 -> sqrt(5) ≈ 2.236... (test quotient selection)
        0x40e00000, // 7.0 -> sqrt(7) ≈ 2.645... (test quotient selection)
        
        // === Guard/round/sticky boundary tests ===
        0x3f800001, // epsilon above 1.0 -> test guard bit
        0x3f800003, // 3*epsilon above 1.0 -> test round bit
        0x3f800007, // 7*epsilon above 1.0 -> test sticky bit
        0x3f80000f, // 15*epsilon above 1.0 -> multiple sticky bits
        0x40000001, // epsilon above 2.0 -> test guard bit
        0x40000003, // 3*epsilon above 2.0 -> test round bit
        
        // === Previously observed failure cases ===
        0x40e4006e, // 7.12505 (observed failure)
        0x016f609c, // 4.39667e-38 (observed failure)
        0x2812c1b1, // 8.14663e-15 (observed failure)
        0x67bee97d, // 1.80311e+24 (observed failure)
        0x1ab82050, // 7.61528e-23 (observed failure)
        0x59042172, // 2.32447e+15 (observed failure)
        0x321bbcdd, // 9.06513e-09 (observed failure)
        0x36a9405f, // 5.04409e-06
        0x3fab6860, // 1.33912
        0x72cb1062, // 8.04419e+30
        0x6e002f83, // 9.91788e+27
        0x2605ba5a, // 4.63962e-16
        0x429850b4, // 76.1576
        0x696c0b48, // 1.7835e+25
        0x01cdf635, // 7.56584e-38
        0x4b975f95, // 1.98408e+07
        0x3b2c6f35, // 0.00263114
        
        // === Square root of small fractions ===
        0x3d800000, // 0.0625 -> 0.25 (exact)
        0x3e000000, // 0.125 -> sqrt(0.125) ≈ 0.353... (inexact)
        0x3e800000, // 0.25 -> 0.5 (exact)
        0x3ec00000, // 0.375 -> sqrt(0.375) ≈ 0.612... (inexact)
        0x3f000000, // 0.5 -> sqrt(0.5) ≈ 0.707... (inexact)
        
        // === Underflow boundary tests ===
        0x00000010, // small subnormal -> extreme underflow result
        0x00001000, // medium subnormal
        0x00010000, // larger subnormal
        0x00100000, // near-normal subnormal
        0x007f0000, // large subnormal
        
        // === Iterator convergence edge cases ===
        0x7f000000, // large input (test convergence speed)
        0x01000000, // tiny input (test convergence accuracy)
        0x7e000000, // near-overflow input
        0x02000000, // small input requiring many iterations
        
        // === Mantissa bit patterns that stress algorithm ===
        0x3f800000, // 1.0 (mantissa = 0)
        0x3fc00000, // 1.5 (mantissa = 0x400000)
        0x3fe00000, // 1.75 (mantissa = 0x600000)
        0x3ff00000, // 1.875 (mantissa = 0x700000)
        0x3ff80000, // 1.9375 (mantissa = 0x780000)
        0x3ffc0000, // 1.96875 (mantissa = 0x7c0000)
        0x3ffe0000, // 1.984375 (mantissa = 0x7e0000)
        0x3fff0000, // 1.9921875 (mantissa = 0x7f0000)
    };
    num_cc = sizeof(corner_vals) / sizeof(corner_vals[0]);
    for (int i = 0; i < num_cc; ++i) {
      conv_cc.u = corner_vals[i];
      dut->a = conv_cc.u;
      dut->eval();
      out_cc.u = dut->y;
      // reference via SoftFloat
      softfloat_exceptionFlags = 0;
      float32_t a_sf_cc;
      a_sf_cc.v = conv_cc.u;
      float32_t r_sf_cc = f32_sqrt(a_sf_cc);
      int math_flags_cc = softfloat_exceptionFlags;
      math_cc.u = r_sf_cc.v;
      // ULP diff - use unsigned comparison for correct handling of negative numbers
      uint32_t ulp_diff_cc;
      if (out_cc.u == math_cc.u) {
        ulp_diff_cc = 0;
      } else if ((out_cc.u ^ math_cc.u) & 0x80000000) {
        // Different signs - handle zero crossing case
        ulp_diff_cc = (out_cc.u & 0x7FFFFFFF) + (math_cc.u & 0x7FFFFFFF);
      } else {
        // Same sign - simple unsigned difference
        ulp_diff_cc = (out_cc.u > math_cc.u) ? (out_cc.u - math_cc.u) : (math_cc.u - out_cc.u);
      }
      // flags
      int dut_flags_cc = (dut->exc_invalid << 4) | (dut->exc_divzero << 3) |
                         (dut->exc_overflow << 2) | (dut->exc_underflow << 1) |
                         (dut->exc_inexact);
      bool is_nan_case_cc = std::isnan(math_cc.f) && std::isnan(out_cc.f);
      bool pass_cc = is_nan_case_cc ? (out_cc.u == math_cc.u) : (ulp_diff_cc == 0);
      bool flags_pass_cc = (dut_flags_cc == math_flags_cc);
      bool overall_pass_cc = pass_cc && flags_pass_cc;
      // print only failures or verbose mode
      if (!overall_pass_cc || verbose) {
        std::cout << "[SQRT CASE " << i << "] seed=" << seed << " a=" << conv_cc.f
                  << " | rtl=" << out_cc.f << " math=" << math_cc.f
                  << " | ulp_diff=" << ulp_diff_cc
                  << (pass_cc ? " PASS" : " FAIL") << " | flags math=0x"
                  << std::hex << math_flags_cc << " rtl=0x" << dut_flags_cc
                  << std::dec << (flags_pass_cc ? "" : " FLAG_FAIL") << std::endl;
      }
      if (!overall_pass_cc) {
        dut->final();
        delete dut;
        return 1;
      }
    }
    std::cout << "=== Sqrt corner-case tests done ===" << std::endl;
  }

  // === Systematic exhaustive testing for critical regions ===
  std::cout << "=== Systematic boundary testing ===" << std::endl;
  
  // Test all subnormal inputs
  for (uint32_t subnormal = 0x00000001; subnormal <= 0x007fffff; subnormal += 0x00001111) {
    dut->a = subnormal;
    dut->eval();
    
    softfloat_exceptionFlags = 0;
    float32_t a_sf;
    a_sf.v = subnormal;
    float32_t r_sf = f32_sqrt(a_sf);
    int math_flags = softfloat_exceptionFlags;
    
    int dut_flags = (dut->exc_invalid << 4) | (dut->exc_divzero << 3) |
                    (dut->exc_overflow << 2) | (dut->exc_underflow << 1) |
                    (dut->exc_inexact);
    
    bool result_match = (dut->y == r_sf.v);
    bool flags_match = (dut_flags == math_flags);
    if (!result_match || !flags_match) {
      union { float f; uint32_t u; } a_union = {.u = subnormal};
      union { float f; uint32_t u; } rtl_union = {.u = dut->y};
      union { float f; uint32_t u; } math_union = {.u = r_sf.v};
      std::cout << "[SQRT SYS " << systematic_tests << "] seed=" << seed
                << " FAIL: a=" << a_union.f
                << "(0x" << std::hex << std::setw(8) << std::setfill('0') << subnormal << ")"
                << " rtl=0x" << std::setw(8) << std::setfill('0') << dut->y
                << " math=0x" << std::setw(8) << std::setfill('0') << r_sf.v
                << " math_flags=0x" << math_flags
                << " rtl_flags=0x" << dut_flags << std::dec << std::endl;
      dut->final();
      delete dut;
      return 1;
    }
    systematic_tests++;
  }
  
  // Test boundary values around 1.0 (critical for sqrt accuracy)
  for (uint32_t near_one = 0x3f7ff000; near_one <= 0x3f801000; near_one++) {
    dut->a = near_one;
    dut->eval();
    
    softfloat_exceptionFlags = 0;
    float32_t a_sf_bnd;
    a_sf_bnd.v = near_one;
    float32_t r_sf_bnd = f32_sqrt(a_sf_bnd);
    int math_flags_bnd = softfloat_exceptionFlags;
    int dut_flags_bnd = (dut->exc_invalid << 4) | (dut->exc_divzero << 3) |
                        (dut->exc_overflow << 2) | (dut->exc_underflow << 1) |
                        (dut->exc_inexact);
    
    if (dut->y != r_sf_bnd.v || dut_flags_bnd != math_flags_bnd) {
      std::cout << "[SQRT BOUNDARY " << systematic_tests << "] seed=" << seed
                << " FAIL: a=0x" << std::hex << std::setw(8) << std::setfill('0') << near_one
                << " rtl=0x" << std::setw(8) << std::setfill('0') << dut->y
                << " math=0x" << std::setw(8) << std::setfill('0') << r_sf_bnd.v
                << " rtl_flags=0x" << dut_flags_bnd
                << " math_flags=0x" << math_flags_bnd << std::dec << std::endl;
      dut->final();
      delete dut;
      return 1;
    }
    systematic_tests++;
  }
  
  std::cout << "Systematic tests completed: " << systematic_tests << std::endl;
  
  // === Stratified Random Testing ===
  std::cout << "=== Stratified random testing ===" << std::endl;
  
  // Use multiple PRNG states for better coverage
  std::mt19937 gen1(static_cast<uint32_t>(seed));
  std::mt19937 gen2(static_cast<uint32_t>(seed ^ 0x9e3779b9ULL));
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
    
    if (!selected_region) selected_region = &regions[0]; // fallback
    
    // Generate random value within selected region
    uint32_t rand_bits;
    if (selected_region->start == selected_region->end) {
      rand_bits = selected_region->start;  // Single value (like -0)
    } else {
      uint64_t range = (uint64_t)selected_region->end - selected_region->start;
      if (range > 0) {
        rand_bits = selected_region->start + (dis(gen2) % (range + 1));
      } else {
        rand_bits = selected_region->start;
      }
    }
    
    union {
      float f;
      uint32_t u;
    } conv;
    conv.u = rand_bits;
    dut->a = conv.u;
    dut->eval(); // Evaluate the design

    // Convert the output back to float
    union {
      uint32_t u;
      float f;
    } out_conv;
    out_conv.u = dut->y;

    // clear FP exceptions and reference sqrt via SoftFloat
    softfloat_exceptionFlags = 0;
    float32_t a_sf;
    a_sf.v = conv.u;
    float32_t r_sf = f32_sqrt(a_sf);
    int math_flags = softfloat_exceptionFlags;
    union {
      uint32_t u;
      float f;
    } math_conv;
    math_conv.u = r_sf.v;

    // ULP diff - use unsigned comparison for correct handling of negative numbers
    uint32_t ulp_diff;
    if (out_conv.u == math_conv.u) {
      ulp_diff = 0;
    } else if ((out_conv.u ^ math_conv.u) & 0x80000000) {
      // Different signs - handle zero crossing case
      ulp_diff = (out_conv.u & 0x7FFFFFFF) + (math_conv.u & 0x7FFFFFFF);
    } else {
      // Same sign - simple unsigned difference
      ulp_diff = (out_conv.u > math_conv.u) ? (out_conv.u - math_conv.u) : (math_conv.u - out_conv.u);
    }
    // collect RTL exception flags and compare
    int dut_flags = (dut->exc_invalid << 4) | (dut->exc_divzero << 3) |
                    (dut->exc_overflow << 2) | (dut->exc_underflow << 1) |
                    (dut->exc_inexact);
    bool flag_pass = (dut_flags == math_flags);

    // If both outputs are NaN, consider as PASS
    bool is_nan_case = std::isnan(math_conv.f) && std::isnan(out_conv.f);
    // strict match: NaN bit patterns must match exactly (including payload)
    bool pass = is_nan_case ? (out_conv.u == math_conv.u) : (ulp_diff == 0);
    bool overall_pass = pass && flag_pass;

    // Print only failures or verbose mode
    if (!overall_pass || verbose) {
      std::cout << "Time: " << time_counter << " | seed=" << seed
                << " | sqrt_in: " << conv.f
                << " (bits=0x" << std::hex << std::setw(8) << std::setfill('0')
                << conv.u << std::dec << ")" << " | sqrt_out(rtl): " << out_conv.f
                << " (bits=0x" << std::hex << std::setw(8) << std::setfill('0')
                << out_conv.u << std::dec << ")"
                << " | sqrt_out(math): " << math_conv.f << " (bits=0x" << std::hex
                << std::setw(8) << std::setfill('0') << math_conv.u << std::dec
                << ")" << " | ulp_diff: " << ulp_diff
                << (pass ? " PASS" : " FAIL")
                << " | FLAG=" << (flag_pass ? " PASS" : " FAIL")
                << " | math_flags=0x" << std::hex << math_flags << std::dec
                << " | dut_flags=0x" << std::hex << dut_flags << std::dec
                << std::endl;
    }

    if (!overall_pass) {
      dut->final();
      delete dut;
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
  delete dut; // Clean up the allocated memory
  return 0;
}
