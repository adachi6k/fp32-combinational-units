# Changelog

All notable changes to this project are documented in this file.

## [1.1.0] - 2026-08-08

- Add strict CLI argument validation to all six testbenches, a `cli-test` Makefile target, and a dedicated `scripts/test_cli_args.sh` regression script, plus CI/README follow-up fixes.
- Add a reproducible CI verification flow (`.github/workflows/verify.yml`) sequencing build, corner/systematic/randomized testbench runs, and CLI argument checks; simplify the Makefile; clean up `fp32_div_comb.sv`.
- Add `fp32_cmp_comb`, a DW_fp_cmp-style fixed-FP32 comparator with relation flags, unordered detection, selectable min/max outputs, per-output status, and randomized self-checking verification.
- Rewrite `fp32_addsub.sv` and `fp32_mul_comb.sv` as independently authored, MIT-only IEEE-754 RTL using conventional floating-point hardware techniques (unpack/classify, sticky-preserving alignment, magnitude add/sub with cancellation handling, leading-zero normalisation, explicit GRS + RNE rounding, gradual underflow, tininess-before-rounding). SoftFloat is used strictly as a black-box test oracle.
- Add combinational FP32 add/subtract/multiply units (`fp32_addsub` core with `fp32_add_comb`/`fp32_sub_comb` wrappers and independent `fp32_mul_comb`), with self-checking testbenches (corner/systematic/stratified-random vs `f32_add`/`f32_sub`/`f32_mul`) and `add`/`sub`/`mul` Makefile targets.

## [1.0.1] - 2026-03-01

- Adopt RISC-V NaN specification: canonical NaN (`0x7FC00000`), no payload propagation. Switch SoftFloat to `SPECIALIZE_TYPE = RISCV`. Document implementation-defined behavior in README.
- Fix testbench silent-pass bugs: add failure exits, strict NaN comparison, result value checks, and boundary test activation.
- Refactor `exp_sum` carry handling in divider; fix off-by-one in random range generation; correct misleading comments.
- Fix `count_lz` default return value from 0 to 24 for all-zero 24-bit input in `fp32_div_comb.sv`.

## [1.0.0] - 2025-06-22

- Initial release: combinational FP32 divider and square-root units with full IEEE-754 compliance, verified against Berkeley SoftFloat.
