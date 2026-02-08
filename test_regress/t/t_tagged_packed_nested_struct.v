// DESCRIPTION: Verilator: Verilog Test module
//
// This file ONLY is placed under the Creative Commons Public Domain, for
// any use, without warranty, 2026 by Wilson Snyder.
// SPDX-License-Identifier: CC0-1.0

// Test packed tagged union with nested unpacked struct member
// (covers V3Width.cpp markNestedStructPacked)

typedef struct { logic [7:0] a; logic [7:0] b; } InnerS;
typedef union tagged packed { void Empty; InnerS Data; } MyUnion;

module t;
    MyUnion u;
    initial begin
        u = tagged Data '{a: 8'h12, b: 8'h34};
        if (u matches tagged Data .d) begin
            if (d.a != 8'h12) $stop;
            if (d.b != 8'h34) $stop;
        end
        $write("*-* All Finished *-*\n");
        $finish;
    end
endmodule
