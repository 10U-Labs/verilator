// DESCRIPTION: Verilator: Verilog Test module
//
// This file ONLY is placed under the Creative Commons Public Domain, for
// any use, without warranty, 2026 by Wilson Snyder.
// SPDX-License-Identifier: CC0-1.0

// Test assignment pattern applied to a ranged basic type (patternBasic)

module t;
  typedef bit [3:0] nibble_t;
  nibble_t n;

  initial begin
    // Assignment pattern on ranged basic type
    n = nibble_t'{0: 1'b1, 1: 1'b0, 2: 1'b1, 3: 1'b0};
    if (n !== 4'b0101) begin
      $display("ERROR: Expected 4'b0101, got 4'b%b", n);
      $stop;
    end

    // Another pattern with default
    n = nibble_t'{default: 1'b1};
    if (n !== 4'b1111) begin
      $display("ERROR: Expected 4'b1111, got 4'b%b", n);
      $stop;
    end

    $write("*-* All Finished *-*\n");
    $finish;
  end
endmodule
