# Tang Nano 9K board build (Gowin GW1NR-9C)

First physical target for BradCore (Falcon). This directory holds the board
top-level, its pin constraints, a yosys `synth_gowin` script, and a
board-level testbench.

**Status: DESIGNED + SYNTHESIZED. Not yet `RUNS_ON_REAL_GATES`.** Synthesis
onto Gowin primitives succeeds and the numbers below are real, but no bitstream
has been generated and no board has been plugged in. See
[What is still missing](#what-is-still-missing).

## Files

| File | What it is |
|------|------------|
| `top_tangnano9k.v` | Board top: power-on reset, core clock divider, boot ROM, data RAM, LED latch, core instance |
| `tangnano9k.cst` | Gowin pin constraints |
| `synth_gowin.ys` | yosys `synth_gowin` script — the synthesis receipt |
| `tb_top_tangnano9k.v` | Board-level testbench: proves the boot program drives the LEDs |

## Board facts

From Sipeed's own `TangNano-9K-example` constraints, cross-checked against
independent references:

| Signal | Pin | Notes |
|--------|-----|-------|
| `sys_clk` | 52 | 27 MHz onboard oscillator |
| `sys_rst_n` | 4 | button S1, active-LOW |
| `led[5:0]` | 10, 11, 13, 14, 15, 16 | **active-LOW**, common anode — invert once at the pin |

The LEDs being active-low is why the top ends with `assign led = ~led_r;`.

## The boot program

Real BradISA machine code in an on-chip ROM — the same encodings the CI
testbench runs, so the board is executing the proved CPU, not a stub:

```
0x00  ADDI r1, r0, 0     0x81000000   init counter
0x04  ADDI r1, r1, 1     0x81100001   loop: r1++
0x08  ADDI r2, r0, 0x04  0x82000004   r2 = data address
0x0C  STW  [r2], r1      0xA0210000   publish the counter
0x10  JMP  0x04          0xD000FFFC   back to the loop
```

The core publishes the counter by storing it to data memory; the top-level
latches `dmem_wdata` on a write and shows the low six bits on the LEDs. So the
visible count is the CPU's register value travelling out through a real memory
write, not a pattern generator pretending to be a CPU.

At 27 MHz this loop would run ~13 million times a second, so `core_clk` is
divided down to one cycle per 0.5 s. Each pass of the loop lands about every
2 seconds: a counter you can actually read with your eyes.

## Running it

```sh
# Board-level simulation (seconds, not minutes)
make sim_board        # from rtl/fpga/ — needs iverilog

# Gowin synthesis (real cell counts)
make gowin            # from rtl/fpga/ — needs yosys
```

`make sim_board` builds with `-DBRAD_FAST_SIM`, which shrinks only the clock
divider constant. The shipped design keeps the real 0.5 s divider, because that
is correct for the board and no simulation can wait for it.

The testbench does not just check "the LEDs moved". It checks that the
published sequence is exactly `1, 2, 3, …` with zero errors, so a core that
stopped counting, published garbage, or never published at all would all fail.

## Measured synthesis results

`yosys 0.52`, `synth_gowin`, yosys stat after mapping. Device is GW1NR-9C:
**8,640 LUT4, 6,480 FF, 26 BSRAM (468 Kb), 20 × 18×18 multipliers.**

| Design | Cells | LUT1–4 | MUX2_LUT5–8 | FFs |
|--------|-------|--------|-------------|-----|
| Full board top | 5,591 | 3,338 | 1,849 | 240 |
| Same design, 32×32 `MUL` removed (probe only) | **956** | 465 | 119 | 240 |
| A bare 32×32 multiplier, on its own | **4,625** | 879 | — | 0 |

**The multiply is the whole problem.** A single 32×32 multiply is 83% of the
entire design. The GW1NR-9C has 20 dedicated 18×18 multipliers — the silicon is
there — but yosys's Gowin flow has no DSP mapping and instead mis-maps `*` onto
the Gowin `ALU` primitive, building the whole thing out of LUTs. Mapped onto the
part as-is, the design needs on the order of 8,400 LUT4s of 8,640, which will
not route. Remove the multiply and the same core is 956 cells: it fits with
room to spare.

Two honest caveats on these numbers:

- They are for **this boot program**. The counter only uses two registers, so
  yosys prunes the other fourteen — hence 240 FFs rather than 512. A program
  using all 16 registers will cost more.
- `MUX2_LUTn` cells each occupy *several* LUT4s, so the true LUT4 count is
  higher than the LUT cell count. Only place-and-route can state it exactly.

## What is still missing

1. **A DSP-aware multiply.** The concrete next RTL task. Either a hand-built
   18×18 decomposition the Gowin flow will map, or a `$mul` that a
   prjtrellis-himbaechel/nextpnr flow picks up as a hard block. The core is
   otherwise comfortably inside the part.
2. **A bitstream.** GW1N place-and-route needs nextpnr-himbaechel, Apicula, or
   Gowin EDA. None is in CI yet. `synth_gowin` deliberately stops at synthesis.
3. **Real program memory.** The core reads instruction memory
   combinationally, so this build uses a small distributed ROM (16 words). Real
   program memory wants a synchronous BSRAM port, which means a matching core
   change — a real design step, not a config tweak.
4. **The board.** Until the bitstream is on real silicon and a person watches
   the LEDs count, the honest tag is DESIGNED + SYNTHESIZED. The
   `RUNS_ON_REAL_GATES` badge stays unflipped.
