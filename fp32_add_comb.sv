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
 * @file    fp32_add_comb.sv
 * @brief   Combinational IEEE-754 Single-Precision Floating-Point Adder
 * @author  adachi6k
 * @date    2025
 *
 * @description
 * Thin wrapper around the shared fp32_addsub core, tying op_sub low so the
 * unit computes y = a + b.  All IEEE-754 semantics and exception flags are
 * produced by the common core and match SoftFloat f32_add bit-for-bit.
 */

// Combinational IEEE-754 Single-Precision Floating-Point Adder
module fp32_add_comb (
    input  logic [31:0] a,             // augend (IEEE-754 FP32)
    input  logic [31:0] b,             // addend (IEEE-754 FP32)

    output logic        exc_invalid,   // invalid operation
    output logic        exc_divzero,   // divide-by-zero (never set for add)
    output logic        exc_overflow,  // result magnitude too large
    output logic        exc_underflow, // result too small (gradual underflow)
    output logic        exc_inexact,   // result not exactly representable

    output logic [31:0] y              // sum a + b (IEEE-754 FP32)
);

  fp32_addsub u_core (
      .a            (a),
      .b            (b),
      .op_sub       (1'b0),
      .exc_invalid  (exc_invalid),
      .exc_divzero  (exc_divzero),
      .exc_overflow (exc_overflow),
      .exc_underflow(exc_underflow),
      .exc_inexact  (exc_inexact),
      .y            (y)
  );

endmodule
