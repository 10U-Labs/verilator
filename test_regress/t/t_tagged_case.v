// DESCRIPTION: Verilator: Verilog Test module
//
// This file ONLY is placed under the Creative Commons Public Domain, for
// any use, without warranty, 2024 by Wilson Snyder.
// SPDX-License-Identifier: CC0-1.0

// Test case pattern matching with tagged unions
// IEEE 1800-2023 Section 12.6.1

`define stop $stop
`define checkh(gotv,expv) do if ((gotv) !== (expv)) begin $write("%%Error: %s:%0d:  got=%0x exp=%0x (%s !== %s)\n", `__FILE__,`__LINE__, (gotv), (expv), `"gotv`", `"expv`"); `stop; end while(0);

module t;

   // Basic tagged union (IEEE example)
   typedef union tagged {
      void Invalid;
      int Valid;
   } VInt;

   // Tagged union with wide types for case matching
   typedef union tagged packed {
      void          Invalid;
      bit [8:1]     Byte8NonZeroLSB;      // Non-zero LSB
      bit [0:31]    Word32LittleEndian;   // Opposite endianness
      bit [59:0]    Wide60;               // 60-bit (33-64 special handling)
      bit [89:0]    Wide90;               // 90-bit (64+ special handling)
   } WideType;

   // Tagged union with unpacked array
   typedef union tagged {
      void        Invalid;
      int         Scalar;
      int         Arr[4];                 // Unpacked array
   } ArrayType;

   VInt v;
   WideType wt;
   ArrayType at;
   int result;
   bit [59:0] wide60_result;
   bit [89:0] wide90_result;

   initial begin
      // Test 1: Basic case matches with void tag
      v = tagged Invalid;
      result = 0;
      case (v) matches
         tagged Invalid : result = 1;
         tagged Valid .n : result = n;
      endcase
      `checkh(result, 1);

      // Test 2: Case matches with value binding
      v = tagged Valid (123);
      result = 0;
      case (v) matches
         tagged Invalid : result = -1;
         tagged Valid .n : result = n;
      endcase
      `checkh(result, 123);

      // Test 3: Wide type case matching - 60-bit
      wt = tagged Wide60 (60'hFEDCBA987654321);
      wide60_result = 0;
      case (wt) matches
         tagged Invalid : wide60_result = 0;
         tagged Byte8NonZeroLSB .b : wide60_result = {52'b0, b};
         tagged Word32LittleEndian .w : wide60_result = {28'b0, w};
         tagged Wide60 .w : wide60_result = w;
         tagged Wide90 .w : wide60_result = w[59:0];
      endcase
      `checkh(wide60_result, 60'hFEDCBA987654321);

      // Test 4: Wide type case matching - 90-bit
      wt = tagged Wide90 (90'hDE_ADBEEFCA_FEBABE12_3456);
      wide90_result = 0;
      case (wt) matches
         tagged Invalid : wide90_result = 0;
         tagged Byte8NonZeroLSB .b : wide90_result = {82'b0, b};
         tagged Word32LittleEndian .w : wide90_result = {58'b0, w};
         tagged Wide60 .w : wide90_result = {30'b0, w};
         tagged Wide90 .w : wide90_result = w;
      endcase
      `checkh(wide90_result, 90'hDE_ADBEEFCA_FEBABE12_3456);

      // Test 5: Non-zero LSB in case match
      wt = tagged Byte8NonZeroLSB (8'hA5);
      result = 0;
      case (wt) matches
         tagged Invalid : result = 0;
         tagged Byte8NonZeroLSB .b : result = b;
         tagged Word32LittleEndian .w : result = w[7:0];
         tagged Wide60 .w : result = w[7:0];
         tagged Wide90 .w : result = w[7:0];
      endcase
      `checkh(result, 8'hA5);

      // Test 6: Opposite endianness in case match
      wt = tagged Word32LittleEndian (32'hDEADBEEF);
      result = 0;
      case (wt) matches
         tagged Invalid : result = 0;
         tagged Byte8NonZeroLSB .b : result = b;
         tagged Word32LittleEndian .w : result = w;
         tagged Wide60 .w : result = w[31:0];
         tagged Wide90 .w : result = w[31:0];
      endcase
      `checkh(result, 32'hDEADBEEF);

      // Test 7: Array type case matching
      at = tagged Arr '{10, 20, 30, 40};
      result = 0;
      case (at) matches
         tagged Invalid : result = -1;
         tagged Scalar .s : result = s;
         tagged Arr .a : result = a[0] + a[1] + a[2] + a[3];
      endcase
      `checkh(result, 100);

      // Test 8: Array type scalar case
      at = tagged Scalar (999);
      result = 0;
      case (at) matches
         tagged Invalid : result = -1;
         tagged Scalar .s : result = s;
         tagged Arr .a : result = a[0];
      endcase
      `checkh(result, 999);

      $write("*-* All Finished *-*\n");
      $finish;
   end

endmodule
