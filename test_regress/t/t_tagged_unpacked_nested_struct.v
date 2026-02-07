// DESCRIPTION: Verilator: Verilog Test module
//
// This file ONLY is placed under the Creative Commons Public Domain, for
// any use, without warranty, 2026 by Wilson Snyder.
// SPDX-License-Identifier: CC0-1.0

// Test unpacked struct as tagged union member (markNestedStructPacked)

module t;
  typedef struct {
    logic [7:0] a;
    logic [7:0] b;
  } UnpackedInner;  // Not declared packed

  typedef union tagged {
    void Empty;
    UnpackedInner Data;
  } MyUnion;

  MyUnion u;

  initial begin
    u = tagged Data '{a: 8'h12, b: 8'h34};

    if (u matches tagged Data .inner) begin
      if (inner.a !== 8'h12 || inner.b !== 8'h34) begin
        $display("ERROR: Expected '{0x12, 0x34}, got '{0x%h, 0x%h}", inner.a, inner.b);
        $stop;
      end
    end else begin
      $display("ERROR: Expected tagged Data");
      $stop;
    end

    $write("*-* All Finished *-*\n");
    $finish;
  end
endmodule
