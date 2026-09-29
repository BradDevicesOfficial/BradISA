// SPDX-License-Identifier: MIT
// BradISA V1 -- BradCore (Falcon 5-stage pipeline)
// Synthesisable RTL for FPGA. Stages: F1→F2→D→EX→WB
// Simple in-order, single-issue, no forwarding (stall on RAW hazards).

`include "bradisa_defines.v"

module brad_core (
    input  wire         clk,
    input  wire         rst_n,
    // Instruction memory port (single-cycle)
    output wire [31:0]  imem_addr,
    input  wire [31:0]  imem_rdata,
    // Data memory port (single-cycle)
    output wire [31:0]  dmem_addr,
    output wire         dmem_req,
    output wire         dmem_we,
    output wire [31:0]  dmem_wdata,
    input  wire [31:0]  dmem_rdata
);

    // ─── Pipeline registers ────────────────────────────────────
    // Stage 1: Fetch
    reg         f_valid;
    reg [31:0]  f_pc;
    reg [31:0]  f_insn;
    // Stage 2: Decode
    reg         d_valid;
    reg [31:0]  d_pc;
    reg [3:0]   d_opcode;
    reg [3:0]   d_rd;
    reg [3:0]   d_rs1;
    reg [3:0]   d_rs2;
    reg [31:0]  d_rs1_val;
    reg [31:0]  d_rs2_val;
    reg [31:0]  d_imm;
    reg         d_reg_we;
    // (no separate E-stage registers: writeback is Decode-driven so the
    //  result commits to the register file in the same cycle the ALU sees it)

    // ─── PC ────────────────────────────────────────────────────
    reg [31:0] pc;
    wire [31:0] next_pc;
    wire        branch_taken;

    // ─── Register file ─────────────────────────────────────────
    // Read ports track the instruction about to enter Decode (raw_* fields of
    // the Fetch-stage instruction), not the stale Decode-stage copy, so the
    // operand captured at the Decode posedge belongs to that instruction.
    wire [3:0]  rf_raddr1 = raw_rs1;
    wire [3:0]  rf_raddr2 = (raw_op == BRAD_OP_STW) ? raw_rs2 : 4'd0;
    wire [31:0] rf_rdata1;
    wire [31:0] rf_rdata2;
    wire        rf_we;
    wire [3:0]  rf_waddr;
    wire [31:0] rf_wdata;

    brad_regfile regfile (
        .clk(clk), .rst_n(rst_n),
        .raddr1(rf_raddr1), .rdata1(rf_rdata1),
        .raddr2(rf_raddr2), .rdata2(rf_rdata2),
        .we(rf_we), .waddr(rf_waddr), .wdata(rf_wdata)
    );

    // ─── Hazard detection ─────────────────────────────────────
    // RAW hazard: the instruction in Fetch reads a register that the
    // instruction in Decode will write at its Decode->Execute commit posedge.
    // Stalling Fetch one cycle lets that write land before the dependent
    // instruction samples the register file.
    wire raw_hazard = f_valid && d_valid && d_reg_we && (d_rd != BRAD_R0) &&
                      ((raw_rs1 == d_rd) || (raw_op == BRAD_OP_STW && raw_rs2 == d_rd));
    wire stall = raw_hazard;

    // ─── Fetch stage ──────────────────────────────────────────
    assign imem_addr = pc;

    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            pc <= 32'd0;
            f_valid <= 1'b0;
            f_pc    <= 32'd0;
            f_insn  <= 32'd0;
        end else if (branch_taken) begin
            // Flush on branch
            f_valid <= 1'b0;
            pc      <= next_pc;
        end else if (!stall) begin
            f_valid <= 1'b1;
            f_pc    <= pc;
            f_insn  <= imem_rdata;
            pc      <= pc + 32'd4;
        end
    end

    // ─── Decode stage ─────────────────────────────────────────
    wire [3:0]  raw_op = f_insn[BRAD_OPCODE_SHIFT +: 4];
    wire [3:0]  raw_rd = f_insn[BRAD_RD_SHIFT     +: 4];
    wire [3:0]  raw_rs1 = f_insn[BRAD_RS1_SHIFT   +: 4];
    wire [3:0]  raw_rs2 = f_insn[BRAD_RS2_SHIFT   +: 4];
    wire [15:0] raw_imm = f_insn[15:0];

    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            d_valid   <= 1'b0;
            d_pc      <= 32'd0;
            d_opcode  <= 4'd0;
            d_rd      <= 4'd0;
            d_rs1     <= 4'd0;
            d_rs2     <= 4'd0;
            d_rs1_val <= 32'd0;
            d_rs2_val <= 32'd0;
            d_imm     <= 32'd0;
            d_reg_we  <= 1'b0;
        end else if (branch_taken) begin
            d_valid <= 1'b0;
        end else if (stall) begin
            // The producer in Decode has already committed its writeback
            // through the Decode-driven write port this posedge; draining
            // Decode lets the Fetch-vs-Decode hazard release cleanly.
            d_valid <= 1'b0;
        end else begin
            d_valid   <= f_valid;
            d_pc      <= f_pc;
            d_opcode  <= raw_op;
            d_rd      <= raw_rd;
            d_rs1     <= raw_rs1;
            d_rs2     <= raw_rs2;
            d_rs1_val <= rf_rdata1;
            d_rs2_val <= rf_rdata2;
            d_imm     <= { {16{raw_imm[15]}}, raw_imm };
            // Writeback enable for ALU/ADDI/LDW (not STW/branches/JMP)
            d_reg_we  <= (raw_op <= BRAD_OP_ADDI) || (raw_op == BRAD_OP_LDW);
        end
    end

    // ─── Execute + Writeback (Decode-driven) ─────────────────
    // The ALU and the writeback data mux are purely combinational on the
    // Decode-stage instruction, so the register-file write commits at the
    // same posedge the instruction leaves Decode for Execute.  The
    // Fetch-vs-Decode hazard above stalls Fetch for exactly one cycle when a
    // dependent instruction would otherwise collide with that write.
    reg  [31:0] alu_result;

    // ALU
    wire [31:0] alu_a = d_rs1_val;
    wire [31:0] alu_b = d_rs2_val;
    always @(*) begin
        case (d_opcode)
            BRAD_OP_ADD: alu_result = alu_a + alu_b;
            BRAD_OP_SUB: alu_result = alu_a - alu_b;
            BRAD_OP_MUL: alu_result = alu_a * alu_b;
            BRAD_OP_AND: alu_result = alu_a & alu_b;
            BRAD_OP_OR:  alu_result = alu_a | alu_b;
            BRAD_OP_XOR: alu_result = alu_a ^ alu_b;
            BRAD_OP_SHL: alu_result = alu_a << alu_b[4:0];
            BRAD_OP_SHR: alu_result = alu_a >> alu_b[4:0];
            default:     alu_result = 32'd0;
        endcase
    end

    // Branch resolution (Decode-stage)
    wire bz_taken   = (d_opcode == BRAD_OP_BZ) && (d_rs1_val == 32'd0);
    wire bnz_taken  = (d_opcode == BRAD_OP_BNZ) && (d_rs1_val != 32'd0);
    wire jmp_taken  = (d_opcode == BRAD_OP_JMP);
    wire call_taken = (d_opcode == BRAD_OP_CALL);
    wire ret_taken  = (d_opcode == BRAD_OP_RET);
    assign branch_taken = d_valid && (bz_taken | bnz_taken | jmp_taken | call_taken | ret_taken);
    // BR/JMP/CALL target: PC+4 + (sext(offset) * 4)  -- offset is a word offset
    wire [31:0] br_target = d_pc + 32'd4 + {d_imm[29:0], 2'b00};
    assign next_pc = ret_taken ? d_rs1_val : br_target;

    // ─── Writeback to register file ───────────────────────────
    // CALL saves PC+4 to the link register; otherwise rd gets the ALU/ADDI
    // result or the single-cycle load data.
    wire [31:0] wb_result = (d_opcode == BRAD_OP_CALL) ? (d_pc + 32'd4)
                          : (d_opcode <= BRAD_OP_SHR) ? alu_result
                          : (d_opcode == BRAD_OP_ADDI) ? (d_rs1_val + d_imm)
                          : 32'd0;
    assign rf_we    = d_valid && ( (d_reg_we && (d_rd != BRAD_R0)) ||
                                   (d_opcode == BRAD_OP_CALL) );
    assign rf_waddr = (d_opcode == BRAD_OP_CALL) ? BRAD_LR : d_rd;
    assign rf_wdata = (d_opcode == BRAD_OP_LDW) ? dmem_rdata : wb_result;

    // Memory port outputs (single-cycle memory; address/data from Decode)
    assign dmem_addr  = d_rs1_val + d_imm;
    assign dmem_req   = d_valid && (d_opcode == BRAD_OP_LDW || d_opcode == BRAD_OP_STW);
    assign dmem_we    = d_valid && (d_opcode == BRAD_OP_STW);
    assign dmem_wdata = d_rs2_val;

endmodule
