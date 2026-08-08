#include "Vfp32_cmp_comb.h"

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <random>
#include <verilated.h>

namespace {

struct Expected {
  bool aeqb;
  bool altb;
  bool agtb;
  bool unordered;
  uint32_t z0;
  uint32_t z1;
  uint8_t status0;
  uint8_t status1;
};

float as_float(uint32_t bits) {
  float value;
  std::memcpy(&value, &bits, sizeof(value));
  return value;
}

uint8_t value_status(uint32_t bits, bool from_a) {
  const bool zero = (bits & 0x7fffffffU) == 0;
  const bool inf = (bits & 0x7fffffffU) == 0x7f800000U;
  const bool nan = (bits & 0x7f800000U) == 0x7f800000U &&
                   (bits & 0x007fffffU) != 0;
  return (static_cast<uint8_t>(from_a) << 7) |
         (static_cast<uint8_t>(nan) << 2) |
         (static_cast<uint8_t>(inf) << 1) |
         static_cast<uint8_t>(zero);
}

Expected reference(uint32_t a, uint32_t b, bool zctr) {
  const float af = as_float(a);
  const float bf = as_float(b);
  Expected expected{};

  expected.unordered = std::isnan(af) || std::isnan(bf);
  if (!expected.unordered) {
    expected.aeqb = af == bf;
    expected.altb = af < bf;
    expected.agtb = af > bf;
  }

  const bool select_a_for_z0 =
      expected.unordered || (expected.agtb && zctr) ||
      (expected.altb && !zctr) || (expected.aeqb && !zctr);
  if (select_a_for_z0) {
    expected.z0 = a;
    expected.z1 = b;
    expected.status0 = value_status(a, true);
    expected.status1 = value_status(b, false);
  } else {
    expected.z0 = b;
    expected.z1 = a;
    expected.status0 = value_status(b, false);
    expected.status1 = value_status(a, true);
  }
  return expected;
}

bool check(Vfp32_cmp_comb &dut, uint32_t a, uint32_t b, bool zctr,
           uint64_t index, bool verbose, uint64_t seed) {
  dut.a = a;
  dut.b = b;
  dut.zctr = zctr;
  dut.eval();

  const Expected expected = reference(a, b, zctr);
  const bool pass =
      dut.aeqb == expected.aeqb && dut.altb == expected.altb &&
      dut.agtb == expected.agtb && dut.unordered == expected.unordered &&
      dut.z0 == expected.z0 && dut.z1 == expected.z1 &&
      dut.status0 == expected.status0 && dut.status1 == expected.status1;

  if (!pass || verbose) {
    std::cout << "seed=" << seed << " case=" << index << " zctr=" << zctr
              << std::hex
              << std::setfill('0') << " a=0x" << std::setw(8) << a
              << " b=0x" << std::setw(8) << b << " z0=0x" << std::setw(8)
              << dut.z0 << "/0x" << std::setw(8) << expected.z0
              << " z1=0x" << std::setw(8) << dut.z1 << "/0x" << std::setw(8)
              << expected.z1 << " status0=0x" << std::setw(2)
              << static_cast<unsigned>(dut.status0) << "/0x" << std::setw(2)
              << static_cast<unsigned>(expected.status0) << " status1=0x"
              << std::setw(2) << static_cast<unsigned>(dut.status1) << "/0x"
              << std::setw(2) << static_cast<unsigned>(expected.status1)
              << std::dec << " relation=" << static_cast<unsigned>(dut.aeqb)
              << static_cast<unsigned>(dut.altb)
              << static_cast<unsigned>(dut.agtb)
              << static_cast<unsigned>(dut.unordered) << "/"
              << expected.aeqb << expected.altb << expected.agtb
              << expected.unordered << (pass ? " PASS" : " FAIL") << '\n';
  }
  return pass;
}

} // namespace

int main(int argc, char **argv) {
  Verilated::commandArgs(argc, argv);
  bool verbose = false;
  uint64_t random_tests = 2000000;
  uint64_t seed = 1;

  if (const char *env = std::getenv("FP32_NUM_TESTS"); env && *env) {
    random_tests = std::strtoull(env, nullptr, 10);
  }
  if (const char *env = std::getenv("FP32_SEED"); env && *env) {
    seed = std::strtoull(env, nullptr, 10);
  }
  for (int i = 1; i < argc; ++i) {
    if (std::strcmp(argv[i], "-v") == 0 ||
        std::strcmp(argv[i], "--verbose") == 0) {
      verbose = true;
    } else if (std::strcmp(argv[i], "--tests") == 0 && i + 1 < argc) {
      random_tests = std::strtoull(argv[++i], nullptr, 10);
    } else if (std::strcmp(argv[i], "--seed") == 0 && i + 1 < argc) {
      seed = std::strtoull(argv[++i], nullptr, 10);
    } else {
      random_tests = std::strtoull(argv[i], nullptr, 10);
    }
  }

  std::cout << "=== IEEE-754 FP32 Combinational Comparator Test Suite ===\n";
  std::cout << "Random test vectors: " << random_tests << '\n';
  std::cout << "Random seed: " << seed << '\n';
  std::cout << "Verbose mode: " << (verbose ? "ON" : "OFF") << '\n';
  std::cout << "==========================================================\n";

  Vfp32_cmp_comb dut;
  constexpr uint32_t corner_values[] = {
      0x00000000, 0x80000000, 0x00000001, 0x80000001, 0x007fffff,
      0x807fffff, 0x00800000, 0x80800000, 0x3f800000, 0xbf800000,
      0x40000000, 0xc0000000, 0x7f7fffff, 0xff7fffff, 0x7f800000,
      0xff800000, 0x7fc00000, 0xffc12345, 0x7fa00000, 0xff800001};

  uint64_t index = 0;
  for (uint32_t a : corner_values) {
    for (uint32_t b : corner_values) {
      for (int zctr = 0; zctr <= 1; ++zctr) {
        if (!check(dut, a, b, zctr != 0, index++, verbose, seed))
          return 1;
      }
    }
  }

  std::mt19937_64 rng(seed);
  for (uint64_t i = 0; i < random_tests; ++i) {
    const uint32_t a = static_cast<uint32_t>(rng());
    const uint32_t b = static_cast<uint32_t>(rng());
    const bool zctr = (rng() & 1U) != 0;
    if (!check(dut, a, b, zctr, index++, verbose, seed))
      return 1;
  }

  dut.final();
  std::cout << "FP32 comparator: " << index << " cases passed\n";
  return 0;
}
