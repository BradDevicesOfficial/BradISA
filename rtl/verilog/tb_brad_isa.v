// SPDX-License-Identifier: MIT
// BradISA V1 -- full-ISA regression for brad_core
//
// The original testbench (tb_brad_core.v) runs ADDI, BNZ and JMP and nothing
// else, which is how ADD/SUB/MUL/AND/OR/XOR/SHL/SHR shipped broken: the
// register-file read port for rs2 was gated to r0 for every opcode except
// STW, so all eight RRR ops quietly computed rd = rs1 op r0.  A green gate
// was telling the truth about the three instructions it actually ran and
// nothing about the other thirteen.
//
// This testbench runs every opcode the core implements and checks each
// result, so "CI is green" now means the whole ISA works.
//
// Encoding (authoritative: the RTL, not docs/04-base-isa.md -- they disagree,
// noted inline where it matters):
//   word = (op<<28) | (rd<<24) | (rs1<<20) | (rs2<<16) | (imm16 & 0xFFFF)
//   branch/jump target = pc + 4 + sext(imm16) * 4
//
// Hand-encoding words is a trap -- writing 0x82000007 believing it means
// "ADDI r1, r0, 7" actually encodes rd=2 and the core dutifully writes r2.
// tools/bradasm exists so nobody has to do this by hand; where this file
// needs a word it is written out in full and checked by CI.

`timescale 1ns / 1ps

module tb_brad_isa;

    reg         clk = 1'b0;
    reg         rst_n = 1'b0;

    reg  [31:0] imem [0:255];
    wire [31:0] imem_addr, imem_rdata;
    assign imem_rdata = imem[imem_addr[9:2]];

    reg  [31:0] dmem [0:255];
    wire [31:0] dmem_addr, dmem_wdata, dmem_rdata;
    wire        dmem_req, dmem_we;
    assign dmem_rdata = dmem_req ? dmem[dmem_addr[9:2]] : 32'd0;

    // Single-cycle data memory write port.
    always @(posedge clk) begin
        if (dmem_we)
            dmem[dmem_addr[9:2]] <= dmem_wdata;
    end

    integer i;
    integer checks = 0;
    integer failed = 0;

    // Encoding helpers, so the words below read as the assembly they are
    // rather than as hex the reader has to decode in their head.
    function [31:0] ri;   // op, rd, rs1, imm
        input [3:0]  op;
        input [3:0]  rd;
        input [3:0]  rs1;
        input [15:0] imm;
        begin ri = {op, rd, rs1, 4'h0, imm}; end
    endfunction

    function [31:0] rrr;  // op, rd, rs1, rs2
        input [3:0]  op;
        input [3:0]  rd;
        input [3:0]  rs1;
        input [3:0]  rs2;
        begin rrr = {op, rd, rs1, rs2, 16'h0000}; end
    endfunction

    brad_core dut (
        .clk(clk), .rst_n(rst_n),
        .imem_addr(imem_addr), .imem_rdata(imem_rdata),
        .dmem_addr(dmem_addr), .dmem_req(dmem_req), .dmem_we(dmem_we),
        .dmem_wdata(dmem_wdata), .dmem_rdata(dmem_rdata)
    );

    always #5 clk = ~clk;

    localparam [3:0] OP_ADD = 4'h0, OP_SUB = 4'h1, OP_MUL = 4'h2,
                     OP_AND = 4'h3, OP_OR  = 4'h4, OP_XOR = 4'h5,
                     OP_SHL = 4'h6, OP_SHR = 4'h7, OP_ADDI= 4'h8,
                     OP_LDW = 4'h9, OP_STW = 4'hA, OP_BZ  = 4'hB,
                     OP_BNZ = 4'hC, OP_JMP = 4'hD, OP_CALL= 4'hE,
                     OP_RET = 4'hF;

    // Fill imem with a NOP (ADDI r0, r0, 0) so a runaway fetch spins in
    // place instead of executing whatever garbage follows the program.
    task clear_mem;
        begin
            for (i = 0; i < 256; i = i + 1) begin
                imem[i] = 32'h81000000;
                dmem[i] = 32'd0;
            end
        end
    endtask

    task boot;
        input integer ns;
        begin
            clk = 1'b0; rst_n = 1'b0;
            #15 rst_n = 1'b1;
            #(ns);
        end
    endtask

    task expect_r;
        input [8*12-1:0] name;
        input integer      rn;
        input [31:0]       want;
        begin
            checks = checks + 1;
            if (dut.regfile.regs[rn] !== want) begin
                failed = failed + 1;
                $display("FAIL %0s: r%0d = 0x%08x (%0d)  want 0x%08x (%0d)",
                         name, rn, dut.regfile.regs[rn], dut.regfile.regs[rn],
                         want, want);
            end
        end
    endtask

    // One RRR op with r2 = 7, r3 = 5, result in r1.
    task run_rrr;
        input [3:0]       op;
        input [31:0]      want;
        input [8*12-1:0]  name;
        begin
            clear_mem;
            imem[0] = ri(OP_ADDI, 2, 0, 16'd7);       // ADDI r2, r0, 7
            imem[1] = ri(OP_ADDI, 3, 0, 16'd5);       // ADDI r3, r0, 5
            imem[2] = rrr(op, 1, 2, 3);               // op  r1, r2, r3
            imem[3] = ri(OP_JMP,  0, 0, 16'hFFFF);     // JMP self
            boot(200);
            expect_r(name, 1, want);
        end
    endtask

    initial begin
        // ── The eight RRR ops ───────────────────────────────────────
        run_rrr(OP_ADD, 32'd12,  "ADD");   // 7 + 5
        run_rrr(OP_SUB, 32'd2,   "SUB");   // 7 - 5
        run_rrr(OP_MUL, 32'd35,  "MUL");   // 7 * 5
        run_rrr(OP_AND, 32'd5,   "AND");   // 7 & 5
        run_rrr(OP_OR,  32'd7,   "OR");    // 7 | 5
        run_rrr(OP_XOR, 32'd2,   "XOR");   // 7 ^ 5
        run_rrr(OP_SHL, 32'd224, "SHL");   // 7 << (5 & 31)
        run_rrr(OP_SHR, 32'd0,   "SHR");   // 7 >> (5 & 31)

        // ── ADDI: negative immediate, and the same-register form ────
        clear_mem;
        imem[0] = ri(OP_ADDI, 2, 0, 16'hFFF9);   // ADDI r2, r0, -7
        imem[1] = ri(OP_ADDI, 1, 0, 16'd7);      // ADDI r1, r0,  7
        imem[2] = ri(OP_ADDI, 1, 1, 16'hFFF9);   // ADDI r1, r1, -7
        imem[3] = ri(OP_JMP,  0, 0, 16'hFFFF);
        boot(200);
        expect_r("ADDI-neg",  2, 32'hFFFFFFF9);
        expect_r("ADDI-rsum", 1, 32'd0);

        // ── r0 is hardwired to zero ──────────────────────────────────
        clear_mem;
        imem[0] = ri(OP_ADDI, 2, 0, 16'd42);     // ADDI r2, r0, 42
        imem[1] = ri(OP_ADDI, 0, 0, 16'hFFFF);   // ADDI r0, r0, -1 (discarded)
        imem[2] = ri(OP_JMP,  0, 0, 16'hFFFF);
        boot(200);
        expect_r("r0-const", 0, 32'd0);
        expect_r("r0-const", 2, 32'd42);

        // ── STW / LDW round trip ─────────────────────────────────────
        // STW keeps its source register in the RS2 field [19:16]; the RD
        // field is unused.  docs/04-base-isa.md says RD -- the RTL wins.
        clear_mem;
        imem[0] = ri(OP_ADDI, 2, 0, 16'h0040);   // ADDI r2, r0, 0x40  (data base)
        imem[1] = ri(OP_ADDI, 3, 0, 16'd42);     // ADDI r3, r0, 42    (value)
        imem[2] = {OP_STW, 4'h0, 4'd2, 4'd3, 16'd8};   // STW [r2+8], r3
        imem[3] = ri(OP_LDW, 4, 2, 16'd8);       // LDW r4, [r2+8]
        imem[4] = ri(OP_JMP,  0, 0, 16'hFFFF);
        boot(400);
        expect_r("STW/LDW", 4, 32'd42);
        expect_r("STW-mem", 2, 32'h40);

        // ── BZ taken: r1 = 0, so the 99 is skipped ───────────────────
        // BZ sits at pc=4 and targets pc=12 => imm = (12-4-4)/4 = 1
        clear_mem;
        imem[0] = ri(OP_ADDI, 1, 0, 16'd0);
        imem[1] = {OP_BZ, 4'h0, 4'd1, 4'h0, 16'd1};  // BZ  r1, +1 word
        imem[2] = ri(OP_ADDI, 1, 0, 16'd99);      // skipped
        imem[3] = ri(OP_ADDI, 1, 0, 16'd7);
        imem[4] = ri(OP_JMP,  0, 0, 16'hFFFF);
        boot(300);
        expect_r("BZ-taken", 1, 32'd7);

        // ── BZ not taken: r1 = 5, so execution falls through ────────
        // The fall-through path runs the 99 and then parks; the branch
        // target is placed after the park so reaching it is the failure.
        // BZ sits at pc=4 and targets pc=16 => imm = (16-4-4)/4 = 2
        clear_mem;
        imem[0] = ri(OP_ADDI, 1, 0, 16'd5);
        imem[1] = {OP_BZ, 4'h0, 4'd1, 4'h0, 16'd2};  // BZ  r1, +2 words
        imem[2] = ri(OP_ADDI, 1, 0, 16'd99);      // runs (fall-through)
        imem[3] = ri(OP_JMP,  0, 0, 16'hFFFF);     // parks here
        imem[4] = ri(OP_ADDI, 1, 0, 16'd7);       // branch target: must not run
        boot(300);
        expect_r("BZ-notaken", 1, 32'd99);

        // ── BNZ taken: r1 = 5, so the 99 is skipped ──────────────────
        // BNZ sits at pc=4 and targets pc=12 => imm = 1
        clear_mem;
        imem[0] = ri(OP_ADDI, 1, 0, 16'd5);
        imem[1] = {OP_BNZ, 4'h0, 4'd1, 4'h0, 16'd1}; // BNZ r1, +1 word
        imem[2] = ri(OP_ADDI, 1, 0, 16'd99);      // skipped
        imem[3] = ri(OP_ADDI, 1, 0, 16'd7);
        imem[4] = ri(OP_JMP,  0, 0, 16'hFFFF);
        boot(300);
        expect_r("BNZ-taken", 1, 32'd7);

        // ── JMP ─────────────────────────────────────────────────────
        // JMP at pc=0 targeting pc=12 => imm = (12-0-4)/4 = 2
        clear_mem;
        imem[0] = {OP_JMP, 4'h0, 4'h0, 4'h0, 16'd2};
        imem[1] = ri(OP_ADDI, 1, 0, 16'd99);      // skipped
        imem[2] = ri(OP_JMP,  0, 0, 16'hFFFF);     // skipped
        imem[3] = ri(OP_ADDI, 1, 0, 16'd7);
        imem[4] = ri(OP_JMP,  0, 0, 16'hFFFF);
        boot(300);
        expect_r("JMP", 1, 32'd7);

        // ── CALL / RET ──────────────────────────────────────────────
        // CALL writes LR = PC+4 = 4.  RET is implemented as "PC <- rs1",
        // so RET must name LR (r14) in its rs1 field: 0xF0E00000.  The
        // spec's fixed 0xF0000000 returns to r0, i.e. address 0.
        //   0  CALL sub         LR = 4,  PC = 16
        //   1  ADDI r1, r0, 1   <- RET lands here
        //   2  JMP self
        //   3  (unused)
        //   4  sub: ADDI r2, r0, 7
        //   5  sub: RET          PC = LR = 4
        clear_mem;
        imem[0] = {OP_CALL, 4'h0, 4'h0, 4'h0, 16'd3};   // CALL +3 words
        imem[1] = ri(OP_ADDI, 1, 0, 16'd1);             // return point
        imem[2] = ri(OP_JMP,  0, 0, 16'hFFFF);
        imem[3] = ri(OP_JMP,  0, 0, 16'hFFFF);
        imem[4] = ri(OP_ADDI, 2, 0, 16'd7);             // subroutine
        imem[5] = {OP_RET, 4'h0, 4'd14, 4'h0, 16'h0000};   // RET -> LR
        imem[6] = ri(OP_JMP,  0, 0, 16'hFFFF);
        imem[7] = ri(OP_JMP,  0, 0, 16'hFFFF);
        boot(400);
        expect_r("CALL/RET", 1, 32'd1);
        expect_r("CALL/RET", 2, 32'd7);
        expect_r("CALL/RET", 14, 32'd4);

        // ── A real loop: r2 = sum(10..1) = 55 ───────────────────────
        //   ADDI r2, r0, 0        ; accumulator
        //   ADDI r3, r0, 10       ; counter
        // loop:
        //   ADD  r2, r2, r3       ; acc += counter
        //   ADDI r3, r3, -1
        //   BNZ  r3, loop         ; BNZ at pc=16, target=8 => imm = -3
        //   JMP  self
        // This is the combination the old testbench never ran: an RRR op
        // whose rs2 was produced one instruction earlier, immediately
        // followed by the ADDI that rewrites that same register.
        clear_mem;
        imem[0] = ri(OP_ADDI, 2, 0, 16'd0);
        imem[1] = ri(OP_ADDI, 3, 0, 16'd10);
        imem[2] = rrr(OP_ADD, 2, 2, 3);                 // r2 = 55
        imem[3] = ri(OP_ADDI, 3, 3, 16'hFFFF);           // r3 -= 1
        imem[4] = {OP_BNZ, 4'h0, 4'd3, 4'h0, 16'hFFFD};     // BNZ r3, -3 words
        imem[5] = ri(OP_JMP,  0, 0, 16'hFFFF);
        // 10 iterations at ~6 cycles each: give it room to actually finish,
        // otherwise the check races the clock and reports a phantom failure.
        boot(2000);
        expect_r("loop-sum", 2, 32'd55);
        expect_r("loop-cnt", 3, 32'd0);

        // ── Summary ─────────────────────────────────────────────────
        $display("ISA checks run   : %0d", checks);
        $display("ISA checks failed : %0d", failed);
        $display("%0s", (failed == 0) ? "PASS" : "FAIL");
        $finish;
    end

endmodule
