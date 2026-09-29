-- SPDX-License-Identifier: MIT
-- BradISA V1 -- full-ISA regression for brad_core (VHDL)
--
-- Mirrors rtl/verilog/tb_brad_isa.v.  The Verilog testbench can reach into
-- the DUT with dut.regfile.regs[n]; GHDL offers no such path, so each program
-- here ends by storing the registers under test to data memory and the
-- checker inspects the memory image instead.  That is a weaker observation
-- (it trusts the store path) but it tests the same instruction semantics and
-- it keeps the two trees honest about each other.
--
-- The same bug is what made this file necessary: the rs2 read port was gated
-- to r0 for every opcode except STW, so all eight RRR ops computed
-- rd = rs1 op r0, and the original testbench -- ADDI, BNZ, JMP only -- stayed
-- green the whole time.
--
-- Encoding (authoritative: the RTL, not docs/04-base-isa.md):
--   word = (op << 28) | (rd << 24) | (rs1 << 20) | (rs2 << 16) | (imm16)
--   branch/jump target = pc + 4 + sext(imm16) * 4

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;
use std.env.all;
use work.bradisa_pkg.all;

entity tb_brad_isa is
end entity tb_brad_isa;

architecture sim of tb_brad_isa is

    signal clk        : std_logic := '0';
    signal rst_n      : std_logic := '0';
    signal imem_addr  : std_logic_vector(31 downto 0);
    signal imem_rdata : std_logic_vector(31 downto 0);
    signal dmem_addr  : std_logic_vector(31 downto 0);
    signal dmem_req   : std_logic;
    signal dmem_we    : std_logic;
    signal dmem_wdata : std_logic_vector(31 downto 0);
    signal dmem_rdata : std_logic_vector(31 downto 0);

    type mem_t is array (0 to 255) of std_logic_vector(31 downto 0);
    signal imem : mem_t := (others => (others => '0'));
    signal dmem : mem_t := (others => (others => '0'));

    constant PERIOD : time := 10 ns;

    -- Opcodes, restated here so the VHDL file stands on its own.
    constant OP_ADD  : std_logic_vector(3 downto 0) := x"0";
    constant OP_SUB  : std_logic_vector(3 downto 0) := x"1";
    constant OP_MUL  : std_logic_vector(3 downto 0) := x"2";
    constant OP_AND  : std_logic_vector(3 downto 0) := x"3";
    constant OP_OR   : std_logic_vector(3 downto 0) := x"4";
    constant OP_XOR  : std_logic_vector(3 downto 0) := x"5";
    constant OP_SHL  : std_logic_vector(3 downto 0) := x"6";
    constant OP_SHR  : std_logic_vector(3 downto 0) := x"7";
    constant OP_ADDI : std_logic_vector(3 downto 0) := x"8";
    constant OP_LDW  : std_logic_vector(3 downto 0) := x"9";
    constant OP_STW  : std_logic_vector(3 downto 0) := x"A";
    constant OP_BZ   : std_logic_vector(3 downto 0) := x"B";
    constant OP_BNZ  : std_logic_vector(3 downto 0) := x"C";
    constant OP_JMP  : std_logic_vector(3 downto 0) := x"D";
    constant OP_CALL : std_logic_vector(3 downto 0) := x"E";
    constant OP_RET  : std_logic_vector(3 downto 0) := x"F";

    -- Scratch addresses the programs store their results to.
    constant SLOT1 : integer := 16;   -- 0x40 -> r1
    constant SLOT2 : integer := 17;   -- 0x44 -> r2
    constant SLOT3 : integer := 18;   -- 0x48 -> r3
    constant SLOTL : integer := 19;   -- 0x4C -> r14 (lr)

    -- Encoding helpers: the words below then read as the assembly they are
    -- rather than as hex the reader has to decode in their head.
    function ri(op : std_logic_vector(3 downto 0);
                rd : std_logic_vector(3 downto 0);
                rs1 : std_logic_vector(3 downto 0);
                imm : std_logic_vector(15 downto 0)) return std_logic_vector is
    begin
        return op & rd & rs1 & x"0" & imm;
    end function;

    function rrr(op : std_logic_vector(3 downto 0);
                 rd : std_logic_vector(3 downto 0);
                 rs1 : std_logic_vector(3 downto 0);
                 rs2 : std_logic_vector(3 downto 0)) return std_logic_vector is
    begin
        return op & rd & rs1 & rs2 & x"0000";
    end function;

    -- STW rsrc, [r0 + addr]  (r0 as base: dmem_addr = 0 + imm)
    function stw_base0(rsrc : std_logic_vector(3 downto 0);
                       addr : std_logic_vector(15 downto 0)) return std_logic_vector is
    begin
        return OP_STW & x"0" & x"0" & rsrc & addr;
    end function;

    function r0 return std_logic_vector is
    begin
        return x"0";
    end function;

begin

    dut: entity work.brad_core
        port map (
            clk => clk, rst_n => rst_n,
            imem_addr => imem_addr, imem_rdata => imem_rdata,
            dmem_addr => dmem_addr, dmem_req => dmem_req,
            dmem_we => dmem_we, dmem_wdata => dmem_wdata,
            dmem_rdata => dmem_rdata
        );

    -- Index reads are guarded with is_x: the address buses are undefined
    -- until the first clock edge after reset, and indexing an array with a
    -- metavalue is a simulation-fatal range error.
    imem_rdata <= imem(to_integer(unsigned(imem_addr(9 downto 2))))
                  when not is_x(imem_addr(9 downto 2)) else (others => '0');
    dmem_rdata <= dmem(to_integer(unsigned(dmem_addr(9 downto 2))))
                  when dmem_req = '1' and not is_x(dmem_addr(9 downto 2))
                  else (others => '0');

    -- ONE driver for dmem, and it clears itself on reset.
    --
    -- mem_t is an array of std_logic_vector, which is a *resolved* type, so
    -- every process that writes dmem contributes a driver and the effective
    -- value is the per-bit resolution of all of them.  std_logic's resolution
    -- table has no entry for 'C' against '0', so a scenario reset holding '0'
    -- plus a store port holding 0x0000000C resolves to 0000000X -- and the
    -- checker then reports a bad load for a store that actually landed.  Not
    -- a core bug and not a memory bug: two drivers writing one resolved
    -- signal.  Clearing on rst_n keeps the reset-between-scenarios behaviour
    -- while leaving exactly one driver.
    process(clk, rst_n)
    begin
        if rst_n = '0' then
            for i in dmem'range loop
                dmem(i) <= (others => '0');
            end loop;
        elsif rising_edge(clk) then
            if dmem_req = '1' and dmem_we = '1' and not is_x(dmem_addr(9 downto 2)) then
                dmem(to_integer(unsigned(dmem_addr(9 downto 2)))) <= dmem_wdata;
            end if;
        end if;
    end process;

    clk <= not clk after PERIOD/2;

    stim: process

        -- Variables, not signals: the procedures below increment them several
        -- times inside one delta cycle, where signal assignment would collapse
        -- the repeated increments into a single deferred update.
        variable checks : integer := 0;
        variable failed : integer := 0;

        procedure clear_mem is
        begin
            for i in 0 to 255 loop
                imem(i) <= ri(OP_ADDI, r0, r0, x"0000");   -- NOP
            end loop;
        end procedure;

        -- Note: rst_n only.  clk already has a concurrent driver, and adding
        -- a second driver that resolves against it pins the clock at '0' --
        -- the core then never sees an edge and every check reads zero.
        procedure boot(constant dur : in time) is
        begin
            rst_n <= '0';
            wait for 15 ns;
            rst_n <= '1';
            wait for dur;
        end procedure;

        procedure chk(constant off  : in integer;
                      constant want : in std_logic_vector(31 downto 0);
                      constant name: in string) is
        begin
            checks := checks + 1;
            if dmem(off) /= want then
                failed := failed + 1;
                report "FAIL " & name & ": got " & to_hstring(dmem(off)) &
                       " want " & to_hstring(want)
                severity note;
            end if;
        end procedure;

        -- One RRR op: r2 = 7, r3 = 5, result in r1, stored to SLOT1.
        procedure run_rrr(constant op   : in std_logic_vector(3 downto 0);
                          constant want : in std_logic_vector(31 downto 0);
                          constant name: in string) is
        begin
            clear_mem;
            imem(0) <= ri(OP_ADDI, x"2", r0, x"0007");
            imem(1) <= ri(OP_ADDI, x"3", r0, x"0005");
            imem(2) <= rrr(op, x"1", x"2", x"3");
            imem(3) <= stw_base0(x"1", x"0040");
            imem(4) <= ri(OP_JMP, r0, r0, x"FFFF");
            boot(300 ns);
            chk(SLOT1, want, name);
        end procedure;

    begin
        -- ── The eight RRR ops ─────────────────────────────────────────
        run_rrr(OP_ADD, x"0000000C", "ADD");   -- 7 + 5
        run_rrr(OP_SUB, x"00000002", "SUB");   -- 7 - 5
        run_rrr(OP_MUL, x"00000023", "MUL");   -- 7 * 5
        run_rrr(OP_AND, x"00000005", "AND");   -- 7 and 5
        run_rrr(OP_OR,  x"00000007", "OR");    -- 7 or 5
        run_rrr(OP_XOR, x"00000002", "XOR");   -- 7 xor 5
        run_rrr(OP_SHL, x"000000E0", "SHL");   -- 7 << (5 and 31)
        run_rrr(OP_SHR, x"00000000", "SHR");   -- 7 >> (5 and 31)

        -- ── ADDI: negative immediate, same-register form, r0 constant ──
        clear_mem;
        imem(0) <= ri(OP_ADDI, x"2", r0, x"FFF9");   -- ADDI r2, r0, -7
        imem(1) <= ri(OP_ADDI, x"1", r0, x"0007");   -- ADDI r1, r0,  7
        imem(2) <= ri(OP_ADDI, x"1", x"1", x"FFF9");-- ADDI r1, r1, -7
        imem(3) <= ri(OP_ADDI, x"2", r0, x"002A");   -- ADDI r2, r0, 42
        imem(4) <= ri(OP_ADDI, r0, r0, x"FFFF");     -- ADDI r0, r0, -1
        imem(5) <= stw_base0(x"1", x"0040");
        imem(6) <= stw_base0(x"2", x"0044");
        imem(7) <= stw_base0(r0,  x"0048");
        imem(8) <= ri(OP_JMP, r0, r0, x"FFFF");
        boot(400 ns);
        chk(SLOT1, x"00000000", "ADDI-rsum");
        chk(SLOT2, x"0000002A", "r0-const");
        chk(SLOT3, x"00000000", "r0-hardwired");

        -- ── STW / LDW round trip ──────────────────────────────────────
        -- STW keeps its source in the RS2 field [19:16]; the RD field is
        -- unused.  docs/04-base-isa.md says RD -- the RTL wins.
        clear_mem;
        imem(0) <= ri(OP_ADDI, x"2", r0, x"0040");   -- ADDI r2, r0, 0x40
        imem(1) <= ri(OP_ADDI, x"3", r0, x"002A");   -- ADDI r3, r0, 42
        imem(2) <= OP_STW & x"0" & x"2" & x"3" & x"0008";     -- STW [r2+8], r3
        imem(3) <= ri(OP_LDW, x"4", x"2", x"0008");   -- LDW r4, [r2+8]
        imem(4) <= stw_base0(x"4", x"0040");
        imem(5) <= stw_base0(x"2", x"0044");
        imem(6) <= ri(OP_JMP, r0, r0, x"FFFF");
        boot(500 ns);
        chk(SLOT1, x"0000002A", "STW/LDW");
        chk(SLOT2, x"00000040", "STW-base");

        -- ── BZ taken: r1 = 0, so the 99 is skipped ────────────────────
        clear_mem;
        imem(0) <= ri(OP_ADDI, x"1", r0, x"0000");
        imem(1) <= OP_BZ & x"0" & x"1" & x"0" & x"0001";   -- BZ r1, +1 word
        imem(2) <= ri(OP_ADDI, x"1", r0, x"0063");         -- skipped
        imem(3) <= ri(OP_ADDI, x"1", r0, x"0007");
        imem(4) <= stw_base0(x"1", x"0040");
        imem(5) <= ri(OP_JMP, r0, r0, x"FFFF");
        boot(400 ns);
        chk(SLOT1, x"00000007", "BZ-taken");

        -- ── BZ not taken: r1 = 5, falls through to the 99 ──────────────
        -- The store has to come before the park, and the branch target sits
        -- after it, so reaching the target is the failure.
        clear_mem;
        imem(0) <= ri(OP_ADDI, x"1", r0, x"0005");
        -- BZ sits at pc=4 and targets pc=20 => imm = (20-4-4)/4 = 3
        imem(1) <= OP_BZ & x"0" & x"1" & x"0" & x"0003";   -- BZ r1, +3 words
        imem(2) <= ri(OP_ADDI, x"1", r0, x"0063");         -- runs (fall-through)
        imem(3) <= stw_base0(x"1", x"0040");              -- records 99
        imem(4) <= ri(OP_JMP, r0, r0, x"FFFF");            -- parks here
        imem(5) <= ri(OP_ADDI, x"1", r0, x"0007");         -- target: must not run
        boot(400 ns);
        chk(SLOT1, x"00000063", "BZ-notaken");

        -- ── BNZ taken ──────────────────────────────────────────────────
        clear_mem;
        imem(0) <= ri(OP_ADDI, x"1", r0, x"0005");
        imem(1) <= OP_BNZ & x"0" & x"1" & x"0" & x"0001";  -- BNZ r1, +1 word
        imem(2) <= ri(OP_ADDI, x"1", r0, x"0063");         -- skipped
        imem(3) <= ri(OP_ADDI, x"1", r0, x"0007");
        imem(4) <= stw_base0(x"1", x"0040");
        imem(5) <= ri(OP_JMP, r0, r0, x"FFFF");
        boot(400 ns);
        chk(SLOT1, x"00000007", "BNZ-taken");

        -- ── JMP ───────────────────────────────────────────────────────
        clear_mem;
        imem(0) <= OP_JMP & x"0" & x"0" & x"0" & x"0002";  -- JMP +2 words
        imem(1) <= ri(OP_ADDI, x"1", r0, x"0063");         -- skipped
        imem(2) <= ri(OP_JMP, r0, r0, x"FFFF");            -- skipped
        imem(3) <= ri(OP_ADDI, x"1", r0, x"0007");
        imem(4) <= stw_base0(x"1", x"0040");
        imem(5) <= ri(OP_JMP, r0, r0, x"FFFF");
        boot(400 ns);
        chk(SLOT1, x"00000007", "JMP");

        -- ── CALL / RET ────────────────────────────────────────────────
        -- RET is implemented as "PC <- rs1", so it must name LR (r14) in
        -- its rs1 field.  The spec's fixed 0xF0000000 returns to r0.
        clear_mem;
        imem(0) <= OP_CALL & x"0" & x"0" & x"0" & x"0003";  -- CALL +3 words
        imem(1) <= ri(OP_ADDI, x"1", r0, x"0001");           -- return point
        imem(2) <= stw_base0(x"1", x"0040");
        imem(3) <= ri(OP_JMP, r0, r0, x"FFFF");
        imem(4) <= ri(OP_ADDI, x"2", r0, x"0007");           -- subroutine
        imem(5) <= stw_base0(x"2", x"0044");
        imem(6) <= stw_base0(x"E", x"004C");                 -- lr = 4
        imem(7) <= OP_RET & x"0" & x"E" & x"0" & x"0000";    -- RET -> LR = 4
        boot(600 ns);
        chk(SLOT1, x"00000001", "CALL/RET");
        chk(SLOT2, x"00000007", "CALL/RET-sub");
        chk(SLOTL, x"00000004", "CALL/RET-lr");

        -- ── A real loop: r2 = sum(10..1) = 55 ──────────────────────────
        clear_mem;
        imem(0) <= ri(OP_ADDI, x"2", r0, x"0000");   -- ADDI r2, r0, 0
        imem(1) <= ri(OP_ADDI, x"3", r0, x"000A");   -- ADDI r3, r0, 10
        imem(2) <= rrr(OP_ADD, x"2", x"2", x"3");    -- ADD r2, r2, r3
        imem(3) <= ri(OP_ADDI, x"3", x"3", x"FFFF");  -- ADDI r3, r3, -1
        imem(4) <= OP_BNZ & x"0" & x"3" & x"0" & x"FFFD";  -- BNZ r3, -3 words
        imem(5) <= stw_base0(x"2", x"0044");
        imem(6) <= stw_base0(x"3", x"0048");
        imem(7) <= ri(OP_JMP, r0, r0, x"FFFF");
        -- 10 iterations at ~6 cycles each; give it room to finish.
        boot(2000 ns);
        chk(SLOT2, x"00000037", "loop-sum");
        chk(SLOT3, x"00000000", "loop-cnt");

        -- ── Summary ───────────────────────────────────────────────────
        report "ISA checks run   : " & integer'image(checks) severity note;
        if failed = 0 then            report "PASS" severity note;
        else
            report "FAIL: " & integer'image(failed) & " of " &
                   integer'image(checks) & " checks failed" severity error;
        end if;
        stop;
        wait;
    end process;

end architecture sim;
