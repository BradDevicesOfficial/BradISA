# Quick-start

This repository ships the **BradISA specification and reference RTL**. It does not bundle a toolchain, and there is no build step in this repository.

## What is here

| Path | What it is |
|---|---|
| `spec/bradisa_spec.tex` | V1 base ISA (normative, LaTeX) |
| `spec/bradisa_v2_ext.tex` | V2 extensions (normative, LaTeX) |
| `rtl/verilog/`, `rtl/vhdl/` | reference core implementations |
| `rtl/fpga/` | Artix-7 / Cyclone V constraints and build scripts |
| `docs/` | the guides you are reading |

## Read the spec

The normative text is the LaTeX source. Where these markdown guides diverge, the spec is the truth.

- Start with [Data model](02-data-model.md) and [Instruction formats](03-instruction-formats.md).
- Then [Base ISA](04-base-isa.md) for the V1 opcode reference.
- [ABI](05-abi.md) covers register roles and the calling convention.

## A first program

BradISA is a scalar 32-bit ISA with 16 general-purpose registers, `r0`–`r15`. A small loop that sums 1..10 into `r1`, in the reference assembly syntax:

```asm
; sum.s — sum 1..10 in r1
.entry start
.org 0
start:
    ADDI r1, r0, 0        ; sum = 0
    ADDI r2, r0, 10       ; counter = 10
loop:
    ADD  r1, r1, r2       ; sum += counter
    ADDI r2, r2, -1       ; counter -= 1
    BNZ  r2, loop         ; loop until counter == 0
    RET
```

The opcodes, operands, and encodings are defined in [Base ISA](04-base-isa.md) and [Instruction formats](03-instruction-formats.md). The reference assembly syntax is summarised in [Toolchain](11-toolchain.md).

## Running a program

There is no runtime bundled in this repository. Two reference paths exist:

1. **Simulate the RTL.** Load `rtl/verilog/brad_core.v` (or the VHDL equivalent) into any Verilog-2001 simulator. The bundled testbench `rtl/verilog/tb_brad_core.v` runs a 1..100 counter loop; see [FPGA](12-fpga.md).
2. **Use the reference assembler and emulator.** The Brad Devices source kit provides a reference assembler (`bradasm`) and a cycle-approximate core emulator (`brad_core_emu`). They are not part of this repository; [Toolchain](11-toolchain.md) documents their interfaces for orientation.

## Next steps

- [Data model](02-data-model.md) — words, bytes, address space, alignment
- [Base ISA](04-base-isa.md) — the 16 instructions, one by one
- [ABI](05-abi.md) — calling convention, register roles, stack discipline
- [VSET](08-vector.md) — the vector / SIMD extension
