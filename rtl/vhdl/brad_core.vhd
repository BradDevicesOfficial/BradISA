-- SPDX-License-Identifier: MIT
-- BradISA V1 -- BradCore (Falcon 5-stage in-order pipeline, VHDL)
-- Synthesisable for FPGA.

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;
use work.bradisa_pkg.all;

entity brad_core is
    port (
        clk         : in  std_logic;
        rst_n       : in  std_logic;
        imem_addr   : out std_logic_vector(31 downto 0);
        imem_rdata  : in  std_logic_vector(31 downto 0);
        dmem_addr   : out std_logic_vector(31 downto 0);
        dmem_req    : out std_logic;
        dmem_we     : out std_logic;
        dmem_wdata  : out std_logic_vector(31 downto 0);
        dmem_rdata  : in  std_logic_vector(31 downto 0)
    );
end entity brad_core;

architecture rtl of brad_core is

    -- Pipeline registers: Fetch
    signal f_valid : std_logic;
    signal f_pc    : std_logic_vector(31 downto 0);
    signal f_insn  : std_logic_vector(31 downto 0);

    -- Pipeline registers: Decode
    signal d_valid    : std_logic;
    signal d_pc       : std_logic_vector(31 downto 0);
    signal d_opcode   : std_logic_vector(3 downto 0);
    signal d_rd       : std_logic_vector(3 downto 0);
    signal d_rs1      : std_logic_vector(3 downto 0);
    signal d_rs2      : std_logic_vector(3 downto 0);
    signal d_rs1_val  : std_logic_vector(31 downto 0);
    signal d_rs2_val  : std_logic_vector(31 downto 0);
    signal d_imm      : std_logic_vector(31 downto 0);
    signal d_reg_we   : std_logic;
    -- (no separate Execute-stage registers: writeback is Decode-driven so the
    --  result commits to the register file in the same cycle the ALU sees it)

    -- PC
    signal pc          : std_logic_vector(31 downto 0);
    signal next_pc     : std_logic_vector(31 downto 0);
    signal branch_taken : std_logic;

    -- Register file
    signal rf_rdata1 : std_logic_vector(31 downto 0);
    signal rf_rdata2 : std_logic_vector(31 downto 0);
    signal rf_raddr2 : std_logic_vector(3 downto 0);
    signal rf_we     : std_logic;
    signal rf_waddr  : std_logic_vector(3 downto 0);
    signal rf_wdata  : std_logic_vector(31 downto 0);

    -- Writeback data mux (combinational on the Decode-stage instruction)
    signal wb_result : std_logic_vector(31 downto 0);

    -- RAW hazard
    signal raw_hazard : std_logic;
    signal stall      : std_logic;

    -- Branch resolution
    signal bz_taken   : std_logic;
    signal bnz_taken  : std_logic;
    signal jmp_taken  : std_logic;
    signal call_taken : std_logic;
    signal ret_taken  : std_logic;
    signal br_target  : std_logic_vector(31 downto 0);

    -- Decode field extraction
    signal raw_op  : std_logic_vector(3 downto 0);
    signal raw_rd  : std_logic_vector(3 downto 0);
    signal raw_rs1 : std_logic_vector(3 downto 0);
    signal raw_rs2 : std_logic_vector(3 downto 0);
    signal raw_imm : std_logic_vector(15 downto 0);

    -- ALU result
    signal alu_result : std_logic_vector(31 downto 0);

    -- ADDI result
    signal addi_result : std_logic_vector(31 downto 0);

begin

    -- ─── Instruction field extraction ────────────────────────
    raw_op  <= f_insn(31 downto 28);
    raw_rd  <= f_insn(27 downto 24);
    raw_rs1 <= f_insn(23 downto 20);
    raw_rs2 <= f_insn(19 downto 16);
    raw_imm <= f_insn(15 downto 0);

    -- ─── Register file instance ──────────────────────────────
    -- Read ports track the incoming (Fetch-stage) instruction's registers so
    -- the operand captured at the Decode posedge belongs to that instruction.
    rf_raddr2 <= raw_rs2 when (raw_op = OP_STW) else REG_R0;

    regfile_inst : entity work.brad_regfile
        port map (
            clk    => clk,
            rst_n  => rst_n,
            raddr1 => raw_rs1,
            rdata1 => rf_rdata1,
            raddr2 => rf_raddr2,
            rdata2 => rf_rdata2,
            we     => rf_we,
            waddr  => rf_waddr,
            wdata  => rf_wdata
        );

    -- ─── Hazard detection ────────────────────────────────────
    -- RAW hazard: the instruction in Fetch reads a register that the
    -- instruction in Decode will write at its Decode->Execute commit posedge.
    -- Stalling Fetch one cycle lets that write land before the dependent
    -- instruction samples the register file.
    raw_hazard <= '1' when (f_valid = '1') and (d_valid = '1') and (d_reg_we = '1')
                            and (d_rd /= REG_R0)
                            and ( (raw_rs1 = d_rd) or ((raw_op = OP_STW) and (raw_rs2 = d_rd)) )
                   else '0';
    stall <= raw_hazard;

    -- ─── Fetch stage ─────────────────────────────────────────
    imem_addr <= pc;

    process(clk, rst_n) begin
        if rst_n = '0' then
            pc      <= (others => '0');
            f_valid <= '0';
            f_pc    <= (others => '0');
            f_insn  <= (others => '0');
        elsif rising_edge(clk) then
            if branch_taken = '1' then
                f_valid <= '0';
                pc      <= next_pc;
            elsif stall = '0' then
                f_valid <= '1';
                f_pc    <= pc;
                f_insn  <= imem_rdata;
                pc      <= std_logic_vector(unsigned(pc) + 4);
            end if;
        end if;
    end process;

    -- ─── Decode stage ────────────────────────────────────────
    addi_result <= std_logic_vector(unsigned(d_rs1_val) + unsigned(d_imm));

    process(clk, rst_n) begin
        if rst_n = '0' then
            d_valid   <= '0';
            d_pc      <= (others => '0');
            d_opcode  <= (others => '0');
            d_rd      <= (others => '0');
            d_rs1     <= (others => '0');
            d_rs2     <= (others => '0');
            d_rs1_val <= (others => '0');
            d_rs2_val <= (others => '0');
            d_imm     <= (others => '0');
            d_reg_we  <= '0';
        elsif rising_edge(clk) then
            if branch_taken = '1' then
                d_valid <= '0';
            elsif stall = '1' then
                -- The producer in Decode has already committed its writeback
                -- through the Decode-driven write port this posedge; draining
                -- Decode lets the Fetch-vs-Decode hazard release cleanly.
                d_valid <= '0';
            else
                d_valid   <= f_valid;
                d_pc      <= f_pc;
                d_opcode  <= raw_op;
                d_rd      <= raw_rd;
                d_rs1     <= raw_rs1;
                d_rs2     <= raw_rs2;
                d_rs1_val <= rf_rdata1;
                d_rs2_val <= rf_rdata2;
                d_imm     <= sext16(raw_imm);
                d_reg_we  <= '1' when (raw_op = OP_ADD) or (raw_op = OP_SUB) or
                                     (raw_op = OP_MUL) or (raw_op = OP_AND) or
                                     (raw_op = OP_OR)  or (raw_op = OP_XOR) or
                                     (raw_op = OP_SHL) or (raw_op = OP_SHR) or
                                     (raw_op = OP_ADDI) or (raw_op = OP_LDW) else '0';
            end if;
        end if;
    end process;

    -- ─── ALU ─────────────────────────────────────────────────
    alu_inst : entity work.brad_alu
        port map (a => d_rs1_val, b => d_rs2_val, op => d_opcode, result => alu_result);

    -- ─── Branch resolution ───────────────────────────────────
    bz_taken   <= '1' when (d_opcode = OP_BZ)  and (d_rs1_val = x"00000000") else '0';
    bnz_taken  <= '1' when (d_opcode = OP_BNZ) and (d_rs1_val /= x"00000000") else '0';
    jmp_taken  <= '1' when d_opcode = OP_JMP  else '0';
    call_taken <= '1' when d_opcode = OP_CALL else '0';
    ret_taken  <= '1' when d_opcode = OP_RET  else '0';

    branch_taken <= d_valid and (bz_taken or bnz_taken or jmp_taken or call_taken or ret_taken);
    -- BR/JMP/CALL target: PC+4 + (sext(offset) * 4)  -- offset is a word offset
    br_target   <= std_logic_vector(unsigned(d_pc) + 4 + shift_left(unsigned(d_imm), 2));
    next_pc     <= d_rs1_val when ret_taken = '1' else br_target;

    -- ─── Execute + Writeback (Decode-driven) ─────────────────
    -- The writeback data mux is purely combinational on the Decode-stage
    -- instruction, so the register-file write commits at the same posedge the
    -- instruction leaves Decode for Execute.  The Fetch-vs-Decode hazard above
    -- stalls Fetch for exactly one cycle when a dependent instruction would
    -- otherwise collide with that write.
    wb_result <= std_logic_vector(unsigned(d_pc) + 4) when (d_opcode = OP_CALL) else
                 alu_result when (d_opcode = OP_ADD) or (d_opcode = OP_SUB) or
                                 (d_opcode = OP_MUL) or (d_opcode = OP_AND) or
                                 (d_opcode = OP_OR)  or (d_opcode = OP_XOR) or
                                 (d_opcode = OP_SHL) or (d_opcode = OP_SHR) else
                 addi_result;

    -- ─── Writeback to register file ──────────────────────────
    -- CALL saves PC+4 to the link register; otherwise rd gets the ALU/ADDI
    -- result or the single-cycle load data.
    rf_we    <= '1' when (d_valid = '1') and ( ((d_reg_we = '1') and (d_rd /= REG_R0)) or (d_opcode = OP_CALL) ) else '0';
    rf_waddr <= REG_LR when (d_opcode = OP_CALL) else d_rd;
    rf_wdata <= dmem_rdata when (d_opcode = OP_LDW) else wb_result;

    -- Memory port outputs (single-cycle memory; address/data from Decode)
    dmem_addr  <= std_logic_vector(unsigned(d_rs1_val) + unsigned(d_imm));
    dmem_req   <= '1' when (d_valid = '1') and ((d_opcode = OP_LDW) or (d_opcode = OP_STW)) else '0';
    dmem_we    <= '1' when (d_valid = '1') and (d_opcode = OP_STW) else '0';
    dmem_wdata <= d_rs2_val;

end architecture rtl;
