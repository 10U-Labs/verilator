// DESCRIPTION: Verilator: Verilog Test module
//
// This file ONLY is placed under the Creative Commons Public Domain, for
// any use, without warranty, 2024 by Wilson Snyder.
// SPDX-License-Identifier: CC0-1.0

// Test error: accessing non-static member variable from static method

module t;
  class Cls;
    int member;  // non-static member variable

    static function void static_func();
      member = 1;
    endfunction
  endclass

  initial begin
    Cls::static_func();
  end
endmodule
