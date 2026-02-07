// DESCRIPTION: Verilator: Verilog Test module
//
// This file ONLY is placed under the Creative Commons Public Domain.
// SPDX-FileCopyrightText: 2021 Wilson Snyder
// SPDX-License-Identifier: CC0-1.0

module t(/*AUTOARG*/
   // Inputs
   clk
   );
   input clk;

   int   cyc = 0;
   reg [15 : 0] t2;

   always@(posedge clk) begin
      if (cyc == 0) begin
         t2 <= 16'd0;
      end
      else if (cyc == 2) begin
         t2 <= 16'habcd;
      end
      else if (cyc == 4) begin
         $display("abcd=%x", t2);
         $display("ab0d=%x", { t2[15:8], 4'd0, t2[3:0] });
         $write("*-* All Finished *-*\n");
         $finish(32'd0);
      end
      // verilator lint_off BLKSEQ
      //
      // Moved ++cyc from a separate always block into this one to fix a
      // cross-block race condition in vltmt (multithreaded) mode.
      //
      // Root cause: When ++cyc was in its own always @(posedge clk) block,
      // V3OrderGraphBuilder (src/V3OrderGraphBuilder.cpp:242-290) created
      // no dependency path between the two blocks. For clocked logic,
      // writes produce Logic->VarStd edges and reads produce Logic->VarPre
      // edges — both outgoing from their respective logic vertices. With
      // no path from the writer block to the reader block, they landed in
      // separate mtasks with no ordering guarantee.
      //
      // The FixDataHazards pass (src/V3OrderParallel.cpp:1780-2057) was
      // designed to catch these cases, but it only tracks writers via
      // VarStd.inEdges() — reader tracking was implemented but then
      // reverted due to performance regression (verilator/verilator#3360).
      // Without reader tracking, the reader block was invisible to
      // FixDataHazards, so no hazard edge was added.
      //
      // The result: thread scheduling could cause this block to see
      // pre-increment cyc at one edge and post-increment cyc at the next,
      // skipping a value entirely (e.g., never observing cyc==2), causing
      // the NBA t2<=16'habcd to never fire and $display to read t2==0.
      //
      // Fix: merge ++cyc into this block (after the if-else chain so cycle
      // numbering is preserved). This eliminates the cross-block data
      // dependency entirely — both the write and read of cyc are now in
      // the same logic vertex, guaranteed to be in the same mtask.
      //
      // verilator lint_on BLKSEQ
      ++cyc;
   end
endmodule
