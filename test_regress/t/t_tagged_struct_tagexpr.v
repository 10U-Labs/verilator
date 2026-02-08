// DESCRIPTION: Verilator: Verilog Test module
//
// This file ONLY is placed under the Creative Commons Public Domain, for
// any use, without warranty, 2026 by Wilson Snyder.
// SPDX-License-Identifier: CC0-1.0

// Test tagged union matching with tagged expressions inside struct patterns
// (covers V3Width.cpp updatePatMemberFromField TaggedExpr branch
// and updateTaggedExprPattern)

typedef struct { int a; int b; } InnerStruct;
typedef union tagged { void None; InnerStruct Data; int Simple; } InnerUnion;
typedef struct { int x; InnerUnion y; } OuterStruct;
typedef union tagged { void Empty; OuterStruct Info; } OuterUnion;

module t;
    OuterUnion ou;
    initial begin
        // Test tagged expr with literal value inside struct pattern
        ou = tagged Info '{x: 5, y: tagged Simple(42)};
        if (ou matches tagged Info '{x: .xv, y: tagged Simple 42}) begin
            if (xv != 5) $stop;
        end

        // Test tagged expr with struct pattern child
        ou = tagged Info '{x: 10, y: tagged Data '{a: 1, b: 2}};
        if (ou matches tagged Info '{x: .xv2, y: tagged Data '{a: .av, b: .bv}}) begin
            if (xv2 != 10) $stop;
            if (av != 1) $stop;
            if (bv != 2) $stop;
        end

        $write("*-* All Finished *-*\n");
        $finish;
    end
endmodule
