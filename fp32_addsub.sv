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
 * @file    fp32_addsub.sv
 * @brief   Combinational IEEE-754 Single-Precision Add/Subtract common core
 * @author  adachi6k
 * @date    2025
 *
 * @description
 * Fully combinational IEEE-754 single-precision floating-point add/subtract
 * unit.  A single common core computes both operations, selected by `op_sub`:
 *   - op_sub == 0 : y = a + b
 *   - op_sub == 1 : y = a - b
 *
 * The datapath is an independently authored register-transfer design using
 * conventional floating-point hardware techniques to implement IEEE-754:
 *
 *   1. Unpack and classify both operands (zero / subnormal / normal / inf / NaN).
 *   2. Build an effective biased exponent and a 24-bit significand for each
 *      operand; subnormals borrow the exponent of the smallest normal and
 *      simply carry a clear integer bit.
 *   3. Order the operands by magnitude, then align the smaller significand with
 *      a generic right-shift-jam so the discarded tail survives as a sticky bit.
 *   4. Add or subtract the magnitudes (subtraction is selected by the XOR of the
 *      two signs, with `b` pre-negated when `op_sub` is asserted).
 *   5. Renormalise with a leading-zero count, bounded by the minimum exponent so
 *      that results below the normal range degrade gradually into subnormals.
 *   6. Round the 24-bit significand using the explicit guard/round/sticky bits
 *      with round-to-nearest-even, then repack.
 *
 * Three extra low-order bits are sufficient for a correctly rounded result:
 * whenever the alignment distance exceeds one the cancellation is at most one
 * bit, and whenever cancellation can be large the alignment discarded nothing.
 *
 * Implementation-defined behaviour (RISC-V choices):
 *   - Rounding mode         : round-to-nearest-even (RNE) only
 *   - Default/canonical NaN : 0x7FC00000 (no payload propagation)
 *   - Signaling NaN         : raises the invalid flag
 *   - Tininess detection    : before rounding
 *   - Underflow             : signalled only when the result is tiny and inexact
 */

// Combinational IEEE-754 Single-Precision Floating-Point Add/Subtract core
module fp32_addsub (
    // Input operands
    input  logic [31:0] a,             // operand A (IEEE-754 FP32)
    input  logic [31:0] b,             // operand B (IEEE-754 FP32)
    input  logic        op_sub,        // 0 => a + b, 1 => a - b

    // IEEE-754 exception flags output
    output logic        exc_invalid,   // invalid operation (inf-inf, sNaN, ...)
    output logic        exc_divzero,   // divide-by-zero (never set for add/sub)
    output logic        exc_overflow,  // result magnitude too large
    output logic        exc_underflow, // result too small (gradual underflow)
    output logic        exc_inexact,   // result not exactly representable

    // Result output
    output logic [31:0] y              // a +/- b (IEEE-754 FP32)
);

  // Rounded value bundled with the status bits this datapath can raise.
  typedef struct packed {
    logic        invalid;
    logic        overflow;
    logic        underflow;
    logic        inexact;
    logic [31:0] y;
  } fp_res_t;

  localparam logic [31:0] QuietNan = 32'h7FC0_0000;  // canonical NaN encoding

  // ---------------------------------------------------------------------------
  // Generic bit-level helpers
  // ---------------------------------------------------------------------------

  // Leading-zero count of a 28-bit word; returns 28 for an all-zero input.
  function automatic int lzc28(input logic [27:0] v);
    int   n;
    logic hit;
    begin
      n   = 0;
      hit = 1'b0;
      for (int i = 27; i >= 0; i--) begin
        if (!hit) begin
          if (v[i]) hit = 1'b1;
          else n = n + 1;
        end
      end
      lzc28 = n;
    end
  endfunction

  // Right shift that collapses everything pushed past the LSB into the LSB, so
  // the "is the discarded tail non-zero" information needed for rounding is
  // preserved for any shift distance.
  function automatic logic [27:0] shift_right_sticky28(input logic [27:0] v, input int n);
    logic [27:0] tail;
    begin
      if (n <= 0) begin
        shift_right_sticky28 = v;
      end else if (n >= 28) begin
        shift_right_sticky28 = {27'd0, |v};
      end else begin
        tail         = v & ~(28'hFFF_FFFF << n);
        shift_right_sticky28 = (v >> n) | {27'd0, |tail};
      end
    end
  endfunction

  // ---------------------------------------------------------------------------
  // Normalise, round and pack.  The argument pair describes the exact value
  //   sig * 2**(exp - 153)
  // where sig[26:3] is the 24-bit significand, sig[2] the guard bit, sig[1:0]
  // the round/sticky pair and sig[27] the carry-out position of a magnitude
  // addition.
  // ---------------------------------------------------------------------------
  function automatic fp_res_t normalize_round_result(input logic sign, input int exp_in,
                                                      input logic [27:0] sig_in);
    int          e;
    logic [27:0] sig;
    int          lz, headroom, lsh, rsh;
    logic        tiny, guard, sticky, lsb, round_up, inexact;
    logic [24:0] sig_r;
    logic [ 7:0] efield;
    logic [30:0] mag;
    fp_res_t     r;
    begin
      r   = '0;
      e   = exp_in;
      sig = sig_in;

      if (sig == 28'd0) begin
        r.y = {sign, 31'd0};
        return r;
      end

      // Absorb a carry out of the significand field.
      if (sig[27]) begin
        sig = (sig >> 1) | {27'd0, sig[0]};
        e   = e + 1;
      end

      // Distance to a normalised significand (leading one at bit 26).  The
      // exponent that an unbounded format would use decides tininess, which is
      // therefore evaluated before any rounding takes place.
      lz   = lzc28(sig) - 1;
      tiny = ((e - lz) < 1);

      // Renormalise, but never push the exponent below the minimum: what cannot
      // be normalised becomes a subnormal result.
      headroom = (e > 1) ? (e - 1) : 0;
      lsh      = (lz < headroom) ? lz : headroom;
      sig      = sig << lsh;
      e        = e - lsh;

      // Values still below the minimum exponent are denormalised into place.
      if (e < 1) begin
        rsh = 1 - e;
        sig = shift_right_sticky28(sig, rsh);
        e   = 1;
      end

      // Round to nearest, ties to even.
      lsb      = sig[3];
      guard    = sig[2];
      sticky   = |sig[1:0];
      inexact  = |sig[2:0];
      round_up = guard & (lsb | sticky);

      sig_r = sig[27:3] + {24'd0, round_up};
      if (sig_r[24]) begin  // rounding carried into the next binade
        sig_r = sig_r >> 1;
        e     = e + 1;
      end

      if (e > 254) begin
        r.overflow = 1'b1;
        r.inexact  = 1'b1;
        r.y        = {sign, 8'hFF, 23'd0};
        return r;
      end

      r.inexact   = inexact;
      r.underflow = tiny & inexact;

      // Summing the whole 24-bit significand onto (exponent - 1) folds the
      // integer bit back into the exponent field, so the subnormal/normal
      // boundary (including a subnormal rounding up to the smallest normal)
      // needs no special case.
      efield = 8'(e - 1);
      mag    = ({23'd0, efield} << 23) + {7'd0, sig_r[23:0]};
      r.y    = {sign, mag};
      return r;
    end
  endfunction

  // ---------------------------------------------------------------------------
  // Operand decode.  Subtraction is performed as an addition of the negated
  // second operand, so only one magnitude datapath is needed.
  // ---------------------------------------------------------------------------
  logic [31:0] bx;
  logic        sign_a, sign_b;
  logic [ 7:0] efld_a, efld_b;
  logic [22:0] frac_a, frac_b;

  assign bx     = op_sub ? {~b[31], b[30:0]} : b;
  assign sign_a = a[31];
  assign sign_b = bx[31];
  assign efld_a = a[30:23];
  assign efld_b = bx[30:23];
  assign frac_a = a[22:0];
  assign frac_b = bx[22:0];

  // Classification
  logic is_nan_a, is_nan_b, is_inf_a, is_inf_b, is_zero_a, is_zero_b;
  logic is_snan_a, is_snan_b;

  assign is_nan_a  = (efld_a == 8'hFF) && (frac_a != 23'd0);
  assign is_nan_b  = (efld_b == 8'hFF) && (frac_b != 23'd0);
  assign is_inf_a  = (efld_a == 8'hFF) && (frac_a == 23'd0);
  assign is_inf_b  = (efld_b == 8'hFF) && (frac_b == 23'd0);
  assign is_zero_a = (efld_a == 8'd0) && (frac_a == 23'd0);
  assign is_zero_b = (efld_b == 8'd0) && (frac_b == 23'd0);
  assign is_snan_a = is_nan_a && !frac_a[22];
  assign is_snan_b = is_nan_b && !frac_b[22];

  // Effective biased exponent and 24-bit significand.  A subnormal is exactly
  // the smallest normal exponent with a clear integer bit.
  logic [ 8:0] exp_a, exp_b;
  logic [23:0] sig_a, sig_b;

  assign exp_a = (efld_a == 8'd0) ? 9'd1 : {1'b0, efld_a};
  assign exp_b = (efld_b == 8'd0) ? 9'd1 : {1'b0, efld_b};
  assign sig_a = {(efld_a != 8'd0), frac_a};
  assign sig_b = {(efld_b != 8'd0), frac_b};

  // Order by magnitude: the alignment shift is then never negative and the sign
  // of an effective subtraction is simply the sign of the larger operand.
  logic        a_is_larger;
  logic [ 8:0] exp_big, exp_small, align_dist;
  logic [23:0] sig_big, sig_small;
  logic        sign_big, eff_sub;

  assign a_is_larger = (exp_a > exp_b) || ((exp_a == exp_b) && (sig_a >= sig_b));
  assign exp_big     = a_is_larger ? exp_a : exp_b;
  assign exp_small   = a_is_larger ? exp_b : exp_a;
  assign sig_big     = a_is_larger ? sig_a : sig_b;
  assign sig_small   = a_is_larger ? sig_b : sig_a;
  assign sign_big    = a_is_larger ? sign_a : sign_b;
  assign eff_sub     = sign_a ^ sign_b;
  assign align_dist  = exp_big - exp_small;

  // Alignment and magnitude add/subtract.  The three low bits are the
  // guard/round/sticky triple; the extra top bit catches the addition carry.
  logic [27:0] mant_big, mant_small, mant_res;
  logic        sign_z;

  assign mant_big   = {1'b0, sig_big, 3'b000};
  assign mant_small = shift_right_sticky28({1'b0, sig_small, 3'b000}, int'(align_dist));
  assign mant_res   = eff_sub ? (mant_big - mant_small) : (mant_big + mant_small);
  assign sign_z     = (eff_sub && (mant_res == 28'd0)) ? 1'b0 : sign_big;

  // ---------------------------------------------------------------------------
  // Special values take priority over the arithmetic datapath.
  // ---------------------------------------------------------------------------
  fp_res_t res;

  always_comb begin
    res = '0;
    if (is_nan_a || is_nan_b) begin
      res.invalid = is_snan_a | is_snan_b;
      res.y       = QuietNan;
    end else if (is_inf_a && is_inf_b) begin
      if (sign_a == sign_b) begin
        res.y = {sign_a, 8'hFF, 23'd0};
      end else begin
        res.invalid = 1'b1;  // opposite infinities have no defined difference
        res.y       = QuietNan;
      end
    end else if (is_inf_a) begin
      res.y = {sign_a, 8'hFF, 23'd0};
    end else if (is_inf_b) begin
      res.y = {sign_b, 8'hFF, 23'd0};
    end else if (is_zero_a && is_zero_b) begin
      // Like-signed zeros keep their sign; opposite zeros give +0 under RNE.
      res.y = (sign_a == sign_b) ? {sign_a, 31'd0} : 32'd0;
    end else if (is_zero_a) begin
      res.y = bx;
    end else if (is_zero_b) begin
      res.y = a;
    end else begin
      res = normalize_round_result(sign_z, int'(exp_big), mant_res);
    end
  end

  assign exc_invalid   = res.invalid;
  assign exc_divzero   = 1'b0;
  assign exc_overflow  = res.overflow;
  assign exc_underflow = res.underflow;
  assign exc_inexact   = res.inexact;
  assign y             = res.y;

endmodule
