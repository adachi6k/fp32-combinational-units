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
 * @file  fp32_cmp_comb.sv
 * @brief Combinational IEEE-754 FP32 comparator with DW_fp_cmp-style outputs
 *
 * `zctr` controls the ordered outputs:
 *   - zctr == 0: z0 = min(a,b), z1 = max(a,b)
 *   - zctr == 1: z0 = max(a,b), z1 = min(a,b)
 *
 * NaN operands make the relation unordered and pass a to z0 and b to z1.
 * status[0], status[1], and status[2] identify zero, infinity, and NaN.
 * status[7] identifies an output selected from operand a.
 */
module fp32_cmp_comb (
    input  logic [31:0] a,
    input  logic [31:0] b,
    input  logic        zctr,

    output logic        aeqb,
    output logic        altb,
    output logic        agtb,
    output logic        unordered,
    output logic [31:0] z0,
    output logic [31:0] z1,
    output logic [ 7:0] status0,
    output logic [ 7:0] status1
);

  logic is_zero_a, is_zero_b;
  logic is_nan_a, is_nan_b;
  logic same_sign;
  logic magnitudes_equal;
  logic magnitude_a_lt_b;
  logic select_a_for_z0;

  assign is_zero_a = (a[30:0] == 31'd0);
  assign is_zero_b = (b[30:0] == 31'd0);
  assign is_nan_a  = (a[30:23] == 8'hff) && (a[22:0] != 23'd0);
  assign is_nan_b  = (b[30:23] == 8'hff) && (b[22:0] != 23'd0);

  assign same_sign        = (a[31] == b[31]);
  assign magnitudes_equal = (a[30:0] == b[30:0]);
  assign magnitude_a_lt_b = (a[30:0] < b[30:0]);

  function automatic logic [7:0] value_status(input logic [30:0] magnitude,
                                               input logic from_a);
    logic is_zero, is_inf, is_nan;
    begin
      is_zero = (magnitude == 31'd0);
      is_inf  = (magnitude[30:23] == 8'hff) && (magnitude[22:0] == 23'd0);
      is_nan  = (magnitude[30:23] == 8'hff) && (magnitude[22:0] != 23'd0);
      value_status = {from_a, 4'b0000, is_nan, is_inf, is_zero};
    end
  endfunction

  always_comb begin
    aeqb      = 1'b0;
    altb      = 1'b0;
    agtb      = 1'b0;
    unordered = is_nan_a | is_nan_b;

    if (!unordered) begin
      if (is_zero_a && is_zero_b) begin
        aeqb = 1'b1;
      end else if (same_sign && magnitudes_equal) begin
        aeqb = 1'b1;
      end else if (a[31] != b[31]) begin
        altb = a[31];
        agtb = b[31];
      end else if (!a[31]) begin
        altb = magnitude_a_lt_b;
        agtb = !magnitude_a_lt_b;
      end else begin
        altb = !magnitude_a_lt_b;
        agtb = magnitude_a_lt_b;
      end
    end

    select_a_for_z0 = unordered
                    | (agtb & zctr)
                    | (altb & ~zctr)
                    | (aeqb & ~zctr);

    if (select_a_for_z0) begin
      z0      = a;
      z1      = b;
      status0 = value_status(a[30:0], 1'b1);
      status1 = value_status(b[30:0], 1'b0);
    end else begin
      z0      = b;
      z1      = a;
      status0 = value_status(b[30:0], 1'b0);
      status1 = value_status(a[30:0], 1'b1);
    end
  end

endmodule
