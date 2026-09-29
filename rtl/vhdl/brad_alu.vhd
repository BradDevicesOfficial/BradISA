-- SPDX-License-Identifier: MIT
-- BradISA V1 -- ALU (VHDL)

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;
use work.bradisa_pkg.all;

entity brad_alu is
    port (
        a      : in  std_logic_vector(31 downto 0);
        b      : in  std_logic_vector(31 downto 0);
        op     : in  std_logic_vector(3 downto 0);
        result : out std_logic_vector(31 downto 0)
    );
end entity brad_alu;

architecture rtl of brad_alu is
    signal a_s, b_s : signed(31 downto 0);
    signal a_u, b_u : unsigned(31 downto 0);

    -- GHDL's numeric_std "*" range-checks and aborts the simulation when the
    -- product does not fit the operand width, so it cannot express "the low
    -- 32 bits of a 32x32 product" -- which is what the ISA specifies and what
    -- the Verilog tree's 32-bit `*` computes.  (This only surfaced now because
    -- MUL had never been executed by any testbench; the ALU reported a bound
    -- check failure the first time the regression reached it.)
    -- Shift-and-add reproduces the truncation explicitly: unsigned "+" wraps
    -- rather than asserting, so each partial product folds into the low word.
    function mul_low32(x, y : unsigned(31 downto 0)) return unsigned is
        variable acc : unsigned(31 downto 0);
        variable msk : unsigned(31 downto 0);
    begin
        acc := (others => '0');
        msk := y;
        for i in 0 to 31 loop
            if x(i) = '1' then
                acc := acc + msk;
            end if;
            msk := shift_left(msk, 1);
        end loop;
        return acc;
    end function;
begin
    a_s <= signed(a); b_s <= signed(b);
    a_u <= unsigned(a); b_u <= unsigned(b);

    process(a, b, op, a_s, b_s, a_u, b_u) begin
        case op is
            when OP_ADD => result <= std_logic_vector(a_u + b_u);
            when OP_SUB => result <= std_logic_vector(a_u - b_u);
            when OP_MUL => result <= std_logic_vector(mul_low32(a_u, b_u));
            when OP_AND => result <= a and b;
            when OP_OR  => result <= a or b;
            when OP_XOR => result <= a xor b;
            when OP_SHL => result <= std_logic_vector(shift_left(a_u, to_integer(b_u(4 downto 0))));
            when OP_SHR => result <= std_logic_vector(shift_right(a_u, to_integer(b_u(4 downto 0))));
            when others => result <= (others => '0');
        end case;
    end process;

end architecture rtl;
