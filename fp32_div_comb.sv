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
 * @file    fp32_div_comb.sv
 * @brief   Combinational IEEE-754 Single-Precision Floating-Point Divider
 * @author  adachi6k
 * @date    2025
 * 
 * @description
 * This module implements a fully combinational IEEE-754 single-precision 
 * floating-point division unit. It implements round-to-nearest-even (RNE)
 * only, with IEEE-754 exception handling and bit-accurate results compared to
 * SoftFloat in the supported verification flow.
 * 
 * Features:
 * - IEEE-754 special-case handling including subnormal numbers
 * - All exception flags (invalid, divide-by-zero, overflow, underflow, inexact)
 * - Round-to-nearest-even (RNE) rounding mode
 * 
 * @note Resource usage: Approximately 25-bit divider + normalization logic
 */

// Combinational IEEE-754 Single-Precision Floating-Point Divider
module fp32_div_comb (
    // Input operands
    input  logic [31:0] a,                 // dividend (IEEE-754 FP32)
    input  logic [31:0] b,                 // divisor (IEEE-754 FP32)
    
    // IEEE-754 exception flags output
    output logic        exc_invalid,       // invalid operation (0/0, inf/inf, etc.)
    output logic        exc_divzero,       // divide-by-zero (finite/0)
    output logic        exc_overflow,      // result magnitude too large
    output logic        exc_underflow,     // result magnitude too small (gradual underflow)
    output logic        exc_inexact,       // result is not exactly representable
    
    // Result output
    output logic [31:0] y                  // quotient (IEEE-754 FP32)
);

  // Debug signals for verification and analysis
  // (accessible from testbench via Verilator public_flat)
  
  // Division core intermediate results
  logic [50:0] dbg_raw_div_full   /*verilator public_flat*/;  // full raw division result
  logic [24:0] dbg_quotient_25b   /*verilator public_flat*/;  // 25-bit quotient
  logic [ 5:0] dbg_leading_zeros  /*verilator public_flat*/;  // leading zeros count
  logic [49:0] dbg_quotient_norm  /*verilator public_flat*/;  // normalized quotient
  logic [23:0] dbg_quotient_final /*verilator public_flat*/;  // final quotient bits
  logic [24:0] dbg_mantissa_work  /*verilator public_flat*/;  // working mantissa
  
  // Rounding control signals
  logic        dbg_guard_bit      /*verilator public_flat*/;  // guard bit for rounding
  logic        dbg_sticky_bit     /*verilator public_flat*/;  // sticky bit for rounding  
  logic        dbg_round_up       /*verilator public_flat*/;  // round up decision
  
  // Subnormal handling debug
  logic [50:0] dbg_subnorm_frac   /*verilator public_flat*/;  // subnormal fraction work
  logic [22:0] dbg_mant_res       /*verilator public_flat*/;
  logic        dbg_guard_s        /*verilator public_flat*/;
  logic        dbg_round_s        /*verilator public_flat*/;
  logic        dbg_sticky_s       /*verilator public_flat*/;
  logic        dbg_round_up_s     /*verilator public_flat*/;
  logic [22:0] dbg_mant_rounded   /*verilator public_flat*/;
  logic [9:0]  dbg_exp_sum        /*verilator public_flat*/;
  logic [9:0]  dbg_exp_unbias     /*verilator public_flat*/;
  logic        dbg_subnormal_path /*verilator public_flat*/;
  logic        dbg_normal_path    /*verilator public_flat*/;

  // unpack
  logic sign_a, sign_b, sign_z;
  logic [7:0] exp_a, exp_b;
  logic [22:0] frac_a, frac_b;
  logic is_zero_a, is_zero_b, is_inf_a, is_inf_b, is_nan_a, is_nan_b;

  assign sign_a = a[31];
  assign sign_b = b[31];
  assign exp_a = a[30:23];
  assign exp_b = b[30:23];
  assign frac_a = a[22:0];
  assign frac_b = b[22:0];

  assign is_zero_a = (exp_a == 0 && frac_a == 0);
  assign is_zero_b = (exp_b == 0 && frac_b == 0);
  assign is_inf_a = (exp_a == 8'hff && frac_a == 0);
  assign is_inf_b = (exp_b == 8'hff && frac_b == 0);
  assign is_nan_a = (exp_a == 8'hff && frac_a != 0);
  assign is_nan_b = (exp_b == 8'hff && frac_b != 0);

  assign sign_z = sign_a ^ sign_b;

  // count leading zeros
  function automatic [4:0] count_lz(input logic [23:0] mant);
    reg [4:0] idx;
    begin
      count_lz = 5'd24;
      for (idx = 5'd23; idx != 5'd31; idx = idx - 1) begin
        if (mant[idx]) begin
          count_lz = 5'd23 - idx;
          break;
        end
      end
    end
  endfunction

  // add function to count leading zeros in 50-bit quotient
  function automatic [5:0] count_lz50(input logic [49:0] mant);
    logic [5:0] i;
    begin
      count_lz50 = 6'd50;
      // count leading zeros with 6-bit index wrap
      for (i = 6'd49; i != 6'd63; i = i - 6'd1) begin
        if (mant[i]) begin
          // subtract with explicit 6-bit result
          count_lz50 = 6'd49 - i;
          break;
        end
      end
    end
  endfunction

  // normalize mantissas
  logic [23:0] norm_a, norm_b;
  logic signed [9:0] exp_unbias /*verilator public*/;
  // leading zero counts for subnormals
  logic [4:0] lz_a, lz_b;
  always_comb begin
    // default leading-zero counts for normalization
    lz_a   = (exp_a == 0) ? count_lz({1'b0, frac_a}) : 5'd0;
    lz_b   = (exp_b == 0) ? count_lz({1'b0, frac_b}) : 5'd0;
    // normalize operands
    norm_a = (exp_a == 0) ? ({1'b0, frac_a} << lz_a) : {1'b1, frac_a};
    norm_b = (exp_b == 0) ? ({1'b0, frac_b} << lz_b) : {1'b1, frac_b};
    // compute unbiased exponent with subnormal adjustment
    if (is_zero_a || is_nan_a || is_nan_b || is_inf_b || is_zero_b) begin
      exp_unbias = 0;
    end else begin
      exp_unbias = $signed({2'b00, (exp_a != 0 ? exp_a : 8'd1)}) - $signed(
          {2'b00, (exp_b != 0 ? exp_b : 8'd1)}) - $signed({5'd0, lz_a}) + $signed({5'd0, lz_b});
    end
  end

  // mantissa division (restoring) + guard/sticky: 50-bit dividend to get 23+2 bits precision
  function automatic [50:0] div_mant(input logic [49:0] num, input logic [23:0] den);
    integer i;
    reg [49:0] q;
    reg [24:0] r;
    logic [24:0] den_ext;
    reg sticky;
    begin
      r = 0;
      q = 0;
      sticky = 0;
      den_ext = {1'b0, den};
      for (i = 49; i >= 0; i = i - 1) begin
        r = {r[23:0], num[i]};
        if (r >= den_ext) begin
          r = r - den_ext;
          q[i] = 1;
        end else begin
          q[i] = 0;
        end
      end
      sticky   = |r;
      div_mant = {sticky, q};  // sticky + 50-bit quotient
    end
  endfunction

  // declare internal division signals
  logic [49:0] opa_div;  // numerator shifted for precision (24 bits mantissa + 26 guard bits)
  logic [23:0] opb_div;  // denominator mantissa
  logic [50:0] raw_div;  // sticky (bit50) + 50-bit quotient
  logic [49:0] q_full;  // 50-bit quotient from divider
  logic [49:0] q_norm;
  logic [23:0] q_div;  // hidden + 23-bit fraction
  logic guard_div;
  logic round_div;
  logic sticky_div;
  // dynamic normalization intermediate signals
  logic sticky_raw_div;  // raw divider sticky bit
  logic [5:0] lz_q;  // leading zero count of raw quotient
  logic [24:0] m;  // temporary mantissa for subnormal normalization

  // rounding and normalization signals
  logic [22:0] frac_rnd_div;  // rounded fraction bits
  logic signed [9:0] exp_sum /*verilator public_flat*/;  // biased exponent after normalization
  logic [24:0] sum_expr;  // intermediate sum for rounding carry
  
  // Rounding control signals
  logic        round_up;      // main path round up decision
  logic        round_up_s;    // subnormal path round up decision
  
  // Subnormal rounding intermediate signals
  integer S;
  logic guard_s, round_s, sticky_s;
  logic [22:0] mant_res;
  logic [50:0] frac_s;  // combined shifted quotient + sticky
  logic [23:0] mant_rounded;  // intermediate rounded mantissa

  // result exponent
  logic [ 7:0] exp_z;
  // intermediate normalization flag (used)
  logic        norm1;

  // main comb logic
  always_comb begin
    // default for flags and exp_sum to avoid latches
    norm1        = '0;
    exp_sum      = '0;
    // default internal signals
    raw_div          = '0;
    opa_div          = '0;
    opb_div          = '0;
    q_div            = '0;
    guard_div        = '0;
    round_div        = '0;
    sticky_div       = '0;
    q_full           = '0;
    q_norm           = '0;
    sum_expr         = '0;
    frac_rnd_div     = '0;
    exp_z            = '0;
    // default dynamic-norm signals to prevent latches
    sticky_raw_div   = '0;
    round_up         = '0;
    lz_q             = '0;
    m                = '0;
    // subnormal defaults
    S                = '0;
    mant_res         = '0;
    guard_s                = '0;
    round_s                = '0;
    sticky_s               = '0;
    round_up_s             = '0;
    frac_s                 = '0;
    mant_rounded           = '0;
    
    // Initialize debug signals
    dbg_quotient_final     = '0;
    dbg_guard_bit          = '0;
    dbg_sticky_bit         = '0;
    dbg_raw_div_full       = '0;
    dbg_quotient_25b       = '0;
    dbg_mantissa_work      = '0;
    dbg_leading_zeros      = '0;
    dbg_quotient_norm      = '0;
    dbg_round_up           = '0;
    
    // Additional debug signal defaults
    dbg_subnorm_frac       = '0;
    dbg_mant_res           = '0;
    dbg_guard_s            = '0;
    dbg_round_s            = '0;
    dbg_sticky_s           = '0;
    dbg_round_up_s         = '0;
    dbg_mant_rounded       = '0;
    dbg_exp_sum            = '0;
    dbg_exp_unbias         = '0;
    dbg_subnormal_path     = '0;
    dbg_normal_path        = '0;
    // default exception flags
    exc_invalid      = '0;
    exc_divzero      = '0;
    exc_overflow     = '0;
    exc_underflow    = '0;
    exc_inexact      = '0;
    // special cases: inf, zero, NaN
    if (is_nan_a || is_nan_b) begin
      // RISC-V: any signaling NaN raises invalid, always return canonical NaN
      exc_invalid = ((is_nan_a && frac_a[22]==1'b0) ||
                     (is_nan_b && frac_b[22]==1'b0)) ? 1'b1 : 1'b0;
      y = 32'h7fc00000;
    end else if (is_inf_a && is_inf_b) begin
      // inf/inf invalid
      exc_invalid = 1;
      y = 32'h7fc00000;
    end else if (is_inf_a) begin
      y = {sign_z, 8'hff, 23'd0};
    end else if (is_zero_a && is_zero_b) begin
      // 0/0 invalid
      exc_invalid = 1'b1;
      y = 32'h7fc00000;
    end else if (is_inf_b) begin
      // finite / inf => zero
      y = {sign_z, 8'd0, 23'd0};
    end else if (is_zero_a && !is_zero_b) begin
      y = {sign_z, 8'd0, 23'd0};
    end else if (is_zero_b) begin
      // finite / 0 => divzero
      y = {sign_z, 8'hff, 23'd0};
      exc_divzero = 1'b1;
    end else begin
      // normalized division with post-div normalization and rounding
      // prepare operands for extended division
      // mantissa division (restoring) and dynamic normalization
      // shift numerator to align for 23-bit mantissa + guard + round bits (total 25 extra bits)
      opa_div          = {norm_a, 25'd0, 1'b0};
      opb_div          = norm_b;
      raw_div          = div_mant(opa_div, opb_div);
      q_full           = raw_div[49:0];
      sticky_raw_div   = raw_div[50];
      lz_q             = count_lz50(q_full);
      q_norm           = q_full << lz_q;
      exp_sum          = exp_unbias + 10'sd150 - $signed({4'd0, lz_q});
      // extract mantissa and rounding bits
      q_div            = {q_norm[49], q_norm[48:26]};
      guard_div        = q_norm[25];
      round_div        = q_norm[24];
      sticky_div            = sticky_raw_div | |q_norm[23:0];
      
      // Debug signal assignments for normal path
      dbg_raw_div_full       = raw_div;
      dbg_quotient_final     = q_div;
      dbg_guard_bit          = guard_div;
      dbg_sticky_bit         = sticky_div;
      dbg_exp_sum            = exp_sum;
      dbg_exp_unbias         = exp_unbias;
      dbg_normal_path        = 1'b1;
      dbg_quotient_25b       = q_norm[49:25];
      dbg_leading_zeros      = lz_q;
      dbg_quotient_norm      = q_norm;
      
      // Compute round-to-nearest-even sum
      round_up               = guard_div & (round_div | sticky_div | q_div[0]);
      sum_expr               = {1'b0, q_div} + {{24{1'b0}}, round_up};
      m                      = sum_expr;  // pre-rounded mantissa with carry bit
      dbg_mantissa_work      = m;  // capture working mantissa in debug output
      dbg_round_up           = round_up;
      // handle rounding carry-out (norm1 tracks carry for exponent adjustment)
      norm1 = sum_expr[24];
      if (norm1) begin
        frac_rnd_div = sum_expr[23:1];
      end else begin
        frac_rnd_div = sum_expr[22:0];
      end
      // final overflow/underflow checks
      // Use exp_sum + norm1 for overflow/normal path (carry-adjusted),
      // but keep exp_sum unchanged for subnormal shift calculation.
      if ((exp_sum + $signed({9'd0, norm1})) > 10'sd254) begin
        // overflow: result too large -> infinity per IEEE754
        exc_overflow = 1'b1;
        exc_inexact  = 1'b1;
        y = {sign_z, 8'hff, 23'd0};
      end else if (exp_sum <= -10'sd24) begin
        // deep underflow: flush to zero and signal underflow & inexact per SoftFloat
        exc_underflow = 1'b1;
        exc_inexact   = 1'b1;
        y = {sign_z, 8'd0, 23'd0};
      end else if (exp_sum <= 10'sd0) begin  // gradual underflow to subnormals (exp <= 0)
         // gradual underflow to subnormal
         // compute shift count for subnormals per SoftFloat: 1 - exp_sum 
         S = 1 - integer'(exp_sum);
         if (S >= 51) begin
           // Too much shift: result is zero
           y = {sign_z, 8'd0, 23'd0};
           exc_underflow = 1'b1;
           exc_inexact   = 1'b1;
         end else begin
           // For subnormal: need to include the hidden bit in the mantissa
           // Shift the entire 24-bit mantissa (including hidden bit)
           frac_s   = ({q_norm, sticky_raw_div}) >> S;
           mant_res = frac_s[49:27];  // Extract 23-bit mantissa (hidden bit becomes part of mantissa)
           guard_s  = frac_s[26];
           round_s  = frac_s[25];
           sticky_s = |frac_s[24:0] | sticky_raw_div;  // Include original sticky from division
           // subnormal result: round to nearest, ties to even (IEEE-754 compliant)
           round_up_s    = guard_s & (round_s | sticky_s | mant_res[0]);
           mant_rounded  = mant_res + {23'd0, round_up_s};
           
           // Debug signal assignments for subnormal path
           dbg_subnormal_path = 1'b1;
           dbg_subnorm_frac   = frac_s;
           dbg_mant_res       = mant_res;
           dbg_guard_s        = guard_s;
           dbg_round_s        = round_s;
           dbg_sticky_s       = sticky_s;
           dbg_round_up_s     = round_up_s;
           dbg_mant_rounded   = mant_rounded[22:0];
           
           // exceptions if any bits lost
           exc_inexact   = guard_s | round_s | sticky_s;
           exc_underflow = exc_inexact;
           // select result (normalize if carry into normal range)
           if (mant_rounded[23]) begin
             y = {sign_z, 8'd1, 23'd0};
           end else begin
             y = {sign_z, 8'd0, mant_rounded[22:0]};
           end
         end
      end else begin
        exp_z = exp_sum[7:0] + {7'd0, norm1};
        // inexact if any rounding bits set
        exc_inexact = guard_div || round_div || sticky_div;
        // underflow if result subnormal after rounding
        if (exp_z == 8'd0) exc_underflow = 1'b1;
        y = {sign_z, exp_z, frac_rnd_div};
      end
    end
    // Post-process: ensure any subnormal result has correct flags
    if (!exc_invalid && !exc_divzero && !exc_overflow && y[30:23] == 8'd0) begin
      // Only signal underflow/inexact for subnormals if there was actual rounding
      // Check if any rounding occurred during computation
      if ((guard_div | round_div | sticky_div) || 
          (guard_s | round_s | sticky_s)) begin
        exc_underflow = 1'b1;
        exc_inexact   = 1'b1;
      end
    end
  end

endmodule
