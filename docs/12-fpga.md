# FPGA Implementation Guide

This document covers the synthesisable Falcon V1 RTL core shipped in `rtl/`. The design implements the complete V1 instruction set in a simple, resource-efficient, single-issue in-order pipeline that you can build, simulate, and load onto real FPGAs today.

## Overview

| Property | Value |
|---|---|
| Target | Falcon (efficiency-class) |
| Pipeline | 5-stage: Fetch → Decode → Execute → Memory → Writeback |
| Issue | single-issue, in-order |
| Hazard model | RAW detect → stall one cycle (no forwarding) |
| Branch penalty | 2 cycles (flush fetch + decode) |
| Register file | 16 × 32-bit, sync write / async read, 2 read + 1 write port |
| ALU ops | ADD, SUB, MUL, AND, OR, XOR, SHL, SHR |
| Finish name | `brad_core` (Verilog, VHDL) |

The Verilog is Verilog-2001; the VHDL is VHDL-2008. Both describe the same design and share the same testbench.

## Files

```
rtl/
  verilog/
    bradisa_defines.v   opcode and control constants (package)
    brad_regfile.v      16 × 32-bit register file
    brad_alu.v          ALU: ADD/SUB/MUL/AND/OR/XOR/SHL/SHR
    brad_core.v         top-level Falcon pipeline
    tb_brad_core.v      testbench (counts 1..100 in a loop)
    falcon_lite_core.v  Falcon-Lite (V2, 3-stage) reference
    kestrel_core.v      Kestrel (V2, 8-stage) reference
  vhdl/
    bradisa_pkg.vhd     VHDL package
    brad_regfile.vhd    register file
    brad_alu.vhd        ALU
    brad_core.vhd       top-level core
    falcon_lite_core.vhd, kestrel_core.vhd, tb_brad_core.vhd
  fpga/
    brad_core.xdc       Xilinx Vivado constraints (Artix-7)
    brad_core.sdc       Intel Quartus constraints (Cyclone V)
    vivado_build.tcl    Vivado build script
    quartus_build.tcl   Quartus build script
    Makefile            build automation
```

## Top-level interface

`brad_core` exposes a minimal memory-interface contract — two single-cycle ports, one for instructions and one for data:

```
clk            input   system clock
rst_n          input   active-low reset
imem_addr[31:0] output  instruction fetch address (word address)
imem_rdata[31:0] input  instruction word at that address
dmem_addr[31:0] output  data-memory address
dmem_req       output  data-memory request strobe
dmem_we        output  write-enable (1 = store, 0 = load)
dmem_wdata[31:0] output store data (for STW)
dmem_rdata[31:0] input  load data (for LDW)
```

An off-chip SRAM or an FPGA block-RAM wrapper hangs off these two ports. All addresses are byte addresses; word alignment is guaranteed for valid programs, so the low two bits can be ignored by the memory wrapper.

## Pipeline

```
 F1 ──► F2 ──► D ──► EX ──► WB
 fetch  fetch  decode  exec+  writeback
 (PC)   (insn) (RF)   memory
```

### Fetch (F1 / F2)

- F1: PC → `imem_addr`
- F2: `imem_rdata` latched as the current instruction

### Decode (D)

- Split the instruction into `opcode[31:28]`, `rd`, `rs1`, `rs2`, immediate
- Read the register file (2 ports → `d_rs1_val`, `d_rs2_val`)
- Decode the ALU operation, branch test, memory request

### Execute + Memory (EX)

- ALU op: compute the result in one cycle
- Load/store: compute `dmem_addr = d_rs1_val + d_imm`, assert `dmem_req`, set `dmem_we`
- Branch: compare the tested register to zero; if taken, redirect PC and flush F1/F2

### Writeback (WB)

- Write `e_result` (or `dmem_rdata` for a load) to `rd`

The EX and WB stages are combined in the RTL (register write happens on the writeback edge), which is why the header comment names the stages **F1→F2→D→EX→WB**.

## Hazards

### RAW hazards

There is **no operand forwarding**. When the decode stage reads a register that the execute stage is about to write, the pipeline stalls:

- Fetch and decode are frozen for one cycle
- The raw register values are re-read on the next cycle

The hazard is detected in `brad_core.v` by comparing `d_rs1` / `d_rs2` against `e_rd` when `e_reg_we` is asserted.

### Branch penalty

Branches resolve in EX. When `branch_taken` is asserted:

- `next_pc` is redirected to the computed target
- The F1 and F2 pipeline registers are flushed (their `_valid` bits cleared)
- **Penalty: 2 cycles** for every taken branch

Simple loops therefore run at roughly 1 instruction per cycle (IPC ≈ 1.0) with only the loop-back branch costing 2 extra cycles.

### No load-use forwarding

A `LDW` followed immediately by a use of its target register stalls 1 cycle. This matches the RAW hazard behaviour and keeps the design small.

## Resource utilisation

Target: Artix-7 35T (`xc7a35ticsg324-1L`), 100 MHz clock.

| Resource | Estimate |
|---|---|
| LUTs | ~300 |
| Flip-flops | ~200 |
| DSP48E1 | 1 (MUL) |
| BRAM | 0 |
| F_max | > 200 MHz |

The core leaves the F_max budget on the table at 100 MHz — the register file is a single 16-entry block with async reads, so the timing wall is the block-RAM or external-SRAM round trip, not the core. Expected silicon F_max is in the 2.4–3.6 GHz class for the Falcon profile when placed into a custom backend.

## Building

### Vivado (Xilinx Artix-7)

```bash
cd rtl/fpga
make vivado            # or: vivado -mode batch -source vivado_build.tcl
```

Default part: `xc7a35ticsg324-1L` (Artix-7 35T). Edit `vivado_build.tcl` to change target.

Bitstream output: `build/vivado/brad_core.bit`.

### Quartus (Intel Cyclone V)

```bash
cd rtl/fpga
make quartus           # or: quartus_sh -t quartus_build.tcl
```

Default device: `5CSXFC6D6F31C6ES` (Cyclone V). Edit `quartus_build.tcl` to change target.

Programming file: `build/quartus/brad_core.sof`.

### Linting

```bash
make lint_verilog      # requires Verilator
make lint_vhdl         # requires GHDL
```

## Simulation

The testbench `tb_brad_core` runs a 100-iteration counter loop and checks the result register at the end.

### Icarus Verilog

```bash
iverilog -o tb_brad_core.vvp bradisa_defines.v brad_regfile.v brad_alu.v brad_core.v tb_brad_core.v
vvp tb_brad_core.vvp
```

### GHDL (VHDL-08)

```bash
ghdl -a --std=08 bradisa_pkg.vhd brad_regfile.vhd brad_alu.vhd brad_core.vhd tb_brad_core.vhd
ghdl -e --std=08 tb_brad_core
ghdl -r --std=08 tb_brad_core
```

### Vivado / Quartus GUI

Add the RTL sources and the testbench, then run behavioural simulation.

## Verifying against the ISA

Pair the RTL with the reference emulator (`brad_core_emu`) for golden-reference testing:

1. Write a BradISA assembly program.
2. Assemble it with the reference assembler (`bradasm`) into instruction words.
3. Load the instruction words as instruction memory in the testbench.
4. Run the reference emulator on the same program.
5. Compare register and memory state after each step.

The normative spec is the golden reference; structural tests check that the RTL matches it instruction-for-instruction. Differences indicate a decode, ALU, or hazard bug.

## V2 reference cores

The `rtl/` tree also ships reference implementations for the other BradISA cores:

| File | Core | Pipeline | Notes |
|---|---|---|---|
| `falcon_lite_core.v` | Falcon-Lite | 3-stage | smallest core; r0–r7 only, no MUL |
| `kestrel_core.v` | Kestrel | 8-stage | dual-issue in-order, BTFNT |

These are released as ISA-referencing references; the fully tuned production cores ship with Brad Silicon.

The GPU-side engine (`bradvector_core.v`) is **not** part of this CPU ISA repository — it lives in the separate **BradVector** repository, which documents the GPU/accelerator ISA.

## Common integration notes

- **Reset:** `rst_n` must be asserted for ≥ 4 cycles to flush all pipeline registers.
- **Word alignment:** the core assumes `imem_addr` / `dmem_addr` are word-aligned. Your memory wrapper can ignore the low 2 bits.
- **Read-only RF address 0:** `r0` is hardwired to zero in `brad_regfile` — writes to `r0` are dropped.
- **STW source:** the decode stage maps `d_rs2` from the RI `RD` field, per the ISA.
- **Branch target:** computed as `pc + 4 + sext(imm16) × 4`; the low 2 bits are forced to zero.