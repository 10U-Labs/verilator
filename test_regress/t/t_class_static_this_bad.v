// DESCRIPTION: Verilator: Verilog Test module
//
// This file ONLY is placed under the Creative Commons Public Domain, for
// any use, without warranty, 2024 by Wilson Snyder.
// SPDX-License-Identifier: CC0-1.0

// Test error: using 'this' in a static method

module t;
  class Cls;
    static function Cls get_this();
      return this;
    endfunction
  endclass

  initial begin
    Cls c;
    c = Cls::get_this();
  end
endmodule
