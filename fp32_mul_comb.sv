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
 * @file    fp32_mul_comb.sv
 * @brief   Combinational IEEE-754 Single-Precision Floating-Point Multiplier
 * @author  adachi6k
 * @date    2025
 *
 * @description
 * Fully combinational IEEE-754 single-precision floating-point multiplier.
 * The datapath is an independently authored register-transfer design using
 * conventional floating-point hardware techniques to implement IEEE-754:
 *
 *   1. Unpack and classify both operands (zero / subnormal / normal / inf / NaN).
 *   2. Normalise each operand into a 24-bit significand whose integer bit is
 *      set, compensating the biased exponent for the shift applied to a
 *      subnormal input.
 *   3. Multiply the two 24-bit significands into a 48-bit product and add the
 *      unbiased exponents.
 *   4. Select the product binade from its top bit, then reduce the 48-bit
 *      product to a 24-bit significand plus explicit guard/round/sticky bits.
 *   5. Renormalise bounded by the minimum exponent (gradual underflow) and
 *      round to nearest-even before repacking.
 *
 * Implementation-defined behaviour (RISC-V choices):
 *   - Rounding mode         : round-to-nearest-even (RNE) only
 *   - Default/canonical NaN : 0x7FC00000 (no payload propagation)
 *   - Signaling NaN         : raises the invalid flag
 *   - Tininess detection    : before rounding
 *   - Underflow             : signalled only when the result is tiny and inexact
 */

// Combinational IEEE-754 Single-Precision Floating-Point Multiplier
module fp32_mul_comb (
    // Input operands
    input  logic [31:0] a,             // multiplicand (IEEE-754 FP32)
    input  logic [31:0] b,             // multiplier   (IEEE-754 FP32)

    // IEEE-754 exception flags output
    output logic        exc_invalid,   // invalid operation (inf * 0, sNaN, ...)
    output logic        exc_divzero,   // divide-by-zero (never set for mul)
    output logic        exc_overflow,  // result magnitude too large
    output logic        exc_underflow, // result too small (gradual underflow)
    output logic        exc_inexact,   // result not exactly representable

    // Result output
    output logic [31:0] y              // product a * b (IEEE-754 FP32)
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

  // Leading-zero count of a 24-bit word; returns 24 for an all-zero input.
  function automatic int lzc24(input logic [23:0] v);
    int   n;
    logic hit;
    begin
      n   = 0;
      hit = 1'b0;
      for (int i = 23; i >= 0; i--) begin
        if (!hit) begin
          if (v[i]) hit = 1'b1;
          else n = n + 1;
        end
      end
      lzc24 = n;
    end
  endfunction

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
  // the round/sticky pair and sig[27] a carry-out position.
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
  // Operand decode and classification
  // ---------------------------------------------------------------------------
  logic        sign_a, sign_b, sign_z;
  logic [ 7:0] efld_a, efld_b;
  logic [22:0] frac_a, frac_b;

  assign sign_a = a[31];
  assign sign_b = b[31];
  assign sign_z = sign_a ^ sign_b;
  assign efld_a = a[30:23];
  assign efld_b = b[30:23];
  assign frac_a = a[22:0];
  assign frac_b = b[22:0];

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

  // ---------------------------------------------------------------------------
  // Normalised 24-bit significands.  A subnormal operand is shifted up until
  // its integer bit is set and its exponent is decremented by the same amount,
  // so the multiplier array always sees a value in [2**23, 2**24).
  // ---------------------------------------------------------------------------
  logic [23:0] raw_a, raw_b;
  int          shl_a, shl_b;
  logic [23:0] msig_a, msig_b;
  int          xexp_a, xexp_b;

  assign raw_a = {(efld_a != 8'd0), frac_a};
  assign raw_b = {(efld_b != 8'd0), frac_b};

  always_comb begin
    shl_a  = (efld_a == 8'd0) ? lzc24(raw_a) : 0;
    shl_b  = (efld_b == 8'd0) ? lzc24(raw_b) : 0;
    msig_a = raw_a << shl_a;
    msig_b = raw_b << shl_b;
    xexp_a = ((efld_a == 8'd0) ? 1 : int'(efld_a)) - shl_a;
    xexp_b = ((efld_b == 8'd0) ? 1 : int'(efld_b)) - shl_b;
  end

  // ---------------------------------------------------------------------------
  // 24 x 24 -> 48-bit significand product.  With both inputs normalised the
  // product lies in [2**46, 2**48), so a single top bit selects the binade.
  // The 24-bit result significand plus guard and round bits are taken from the
  // top of the product and everything below them is compressed into a sticky.
  // ---------------------------------------------------------------------------
  logic [47:0] prod;
  logic [27:0] mant_p;
  int          exp_p;

  assign prod = {24'd0, msig_a} * {24'd0, msig_b};

  always_comb begin
    if (prod[47]) begin
      mant_p = {1'b0, prod[47:24], prod[23], prod[22], |prod[21:0]};
      exp_p  = xexp_a + xexp_b - 126;
    end else begin
      mant_p = {1'b0, prod[46:23], prod[22], prod[21], |prod[20:0]};
      exp_p  = xexp_a + xexp_b - 127;
    end
  end

  // ---------------------------------------------------------------------------
  // Special values take priority over the arithmetic datapath.
  // ---------------------------------------------------------------------------
  fp_res_t res;

  always_comb begin
    res = '0;
    if (is_nan_a || is_nan_b) begin
      res.invalid = is_snan_a | is_snan_b;
      res.y       = QuietNan;
    end else if ((is_inf_a && is_zero_b) || (is_zero_a && is_inf_b)) begin
      res.invalid = 1'b1;  // infinity times zero has no defined product
      res.y       = QuietNan;
    end else if (is_inf_a || is_inf_b) begin
      res.y = {sign_z, 8'hFF, 23'd0};
    end else if (is_zero_a || is_zero_b) begin
      res.y = {sign_z, 31'd0};
    end else begin
      res = normalize_round_result(sign_z, exp_p, mant_p);
    end
  end

  assign exc_invalid   = res.invalid;
  assign exc_divzero   = 1'b0;
  assign exc_overflow  = res.overflow;
  assign exc_underflow = res.underflow;
  assign exc_inexact   = res.inexact;
  assign y             = res.y;

endmodule
