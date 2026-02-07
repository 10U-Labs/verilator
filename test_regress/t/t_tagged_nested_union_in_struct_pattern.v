// DESCRIPTION: Verilator: Verilog Test module
//
// This file ONLY is placed under the Creative Commons Public Domain, for
// any use, without warranty, 2026 by Wilson Snyder.
// SPDX-License-Identifier: CC0-1.0

// Test struct pattern with tagged union fields using AstTaggedExpr syntax
// (tagged expression with assignment pattern or literal inside struct pattern)

module t;
  // Inner struct for complex tagged union member
  typedef struct packed {
    logic [7:0] x;
    logic [7:0] y;
  } Point;

  // Tagged unions used as struct fields
  typedef union tagged {
    void None;
    int Value;
  } OptInt;

  typedef union tagged {
    void Empty;
    Point Coord;
  } OptPoint;

  // Outer struct containing tagged union fields
  typedef struct {
    int id;
    OptInt simple;
    OptPoint complex_field;
  } Record;

  // Top-level tagged union wrapping the struct
  typedef union tagged {
    void Nil;
    Record Data;
  } Outer;

  Outer o;
  int errors = 0;

  initial begin
    // Assign with nested tagged expressions
    o = tagged Data '{id: 1, simple: tagged Value 42, complex_field: tagged Coord '{x: 8'hAA, y: 8'hBB}};

    // Matches with struct pattern containing AstTaggedExpr:
    // - "tagged Value 99" is AstTaggedExpr (literal, not patternNoExpr)
    // - "tagged Coord '{x: .cx, y: .cy}" is AstTaggedExpr (assignment pattern)
    // This exercises updatePatMemberFromField -> AstTaggedExpr branch (lines 5194-5202)
    // and updateTaggedExprPattern with both Pattern and non-Pattern children
    if (o matches tagged Data '{id: .a, simple: tagged Value 42, complex_field: tagged Coord '{x: .cx, y: .cy}}) begin
      if (a !== 1) begin
        $display("ERROR: id mismatch: %0d", a);
        errors++;
      end
      if (cx !== 8'hAA || cy !== 8'hBB) begin
        $display("ERROR: coord mismatch: %0h, %0h", cx, cy);
        errors++;
      end
    end else begin
      $display("ERROR: Pattern did not match");
      errors++;
    end

    if (errors == 0) begin
      $write("*-* All Finished *-*\n");
    end
    $finish;
  end
endmodule
