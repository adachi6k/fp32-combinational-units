#!/usr/bin/env bash

set -u -o pipefail

failures=0

expect_failure() {
  local label=$1
  shift

  printf 'cli-test: %-58s' "$label"
  if "$@" >/dev/null 2>&1; then
    echo 'FAIL (expected non-zero exit)'
    failures=$((failures + 1))
  else
    echo 'ok'
  fi
}

overflow=18446744073709551616

for operation in add sub mul cmp div sqrt; do
  executable="./obj_dir/${operation}/Vfp32_${operation}_comb"

  expect_failure "${operation}: --tests without value" "$executable" --tests
  expect_failure "${operation}: --seed without value" "$executable" --seed
  expect_failure "${operation}: unknown option" "$executable" --unknown
  expect_failure "${operation}: non-numeric positional" "$executable" not_a_number
  expect_failure "${operation}: negative --tests" "$executable" --tests -1
  expect_failure "${operation}: negative --seed" "$executable" --seed -1
  expect_failure "${operation}: overflowing --tests" "$executable" --tests "$overflow"
  expect_failure "${operation}: overflowing --seed" "$executable" --seed "$overflow"
  expect_failure "${operation}: invalid FP32_NUM_TESTS" env FP32_NUM_TESTS=bad "$executable"
  expect_failure "${operation}: negative FP32_NUM_TESTS" env FP32_NUM_TESTS=-1 "$executable"
  expect_failure "${operation}: overflowing FP32_NUM_TESTS" env FP32_NUM_TESTS="$overflow" "$executable"
  expect_failure "${operation}: invalid FP32_SEED" env FP32_SEED=bad "$executable"
  expect_failure "${operation}: negative FP32_SEED" env FP32_SEED=-1 "$executable"
  expect_failure "${operation}: overflowing FP32_SEED" env FP32_SEED="$overflow" "$executable"
done

if (( failures != 0 )); then
  echo "cli-test: ${failures} invalid-argument check(s) failed"
  exit 1
fi

echo 'cli-test: all invalid-argument checks passed'
