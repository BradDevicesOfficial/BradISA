# Toolchain and Reference Implementations

This repository ships the BradISA **specification and RTL** only. It does not bundle a toolchain, build binaries, or install anything. This document describes the reference assembler and emulator that exist in the Brad Devices source kit, so the reference assembly syntax and the software model are documented alongside the ISA.

## What ships in this repository

| Component | Status |
|---|---|
| `spec/bradisa_spec.tex`, `spec/bradisa_v2_ext.tex` | normative ISA specification |
| `rtl/verilog/`, `rtl/vhdl/` | reference core RTL |
| `rtl/fpga/` | FPGA constraints and build scripts |
| `docs/` | programmer-visible guides |

There is no `bradc`, `bradvm`, `bradgdb`, or `braddev` in this repository, and no `src/` tree to build.

## Reference implementations (source kit, not bundled)

The Brad Devices source kit contains two reference implementations for the CPU ISA:

| Component | Role |
|---|---|
| **bradasm** | assembler: source text → instruction words + symbol table + entry point |
| **brad_core_emu** | cycle-approximate core and SoC emulator |

They are named here for orientation. They are not downloaded or built by this repository, and the ISA specification remains normative.

## Reference assembly syntax (bradasm)

### Syntax

| Syntax | Meaning |
|---|---|
| `; …` | comment to end of line |
| `name:` | label |
| `.org N` | set the current address |
| `.word expr` | emit a 32-bit word |
| `.byte expr` | emit a byte |
| `.half expr` | emit a 16-bit halfword |
| `.entry expr` | set the entry point |
| `.compress` / `.nocompress` | enable / disable 16-bit compressed emission |

Registers are `r0`–`r15` (32-bit). Mnemonics are case-insensitive.

### Instructions

| Form | Example | Notes |
|---|---|---|
| RRR | `ADD r1, r2, r3` | also `SUB`, `MUL`, `AND`, `OR`, `XOR`, `SHL`, `SHR` |
| RI | `ADDI r1, r2, 8` | immediate form |
| load | `LDW r1, [r2 + 4]` | or `LDW r1, r2, 4` |
| store | `STW r3, [r2 + 4]` | or `STW r3, r2, 4` |
| branch | `BZ r1, label` / `BNZ r1, label` | PC-relative, halfword units |
| jump / call | `JMP label` / `CALL label` | |
| return | `RET` | |
| compressed | `C.ADD`, `C.ADDI`, `C.LDW`, `C.STW`, `C.BZ`, `C.BNZ`, `C.MV`, `C.NOP`, `C.JMP`, `C.CALL` | 16-bit forms |

Instruction encodings — the RRR / RI / BR / JMP / RET families — are defined in [Instruction formats](03-instruction-formats.md) and in the spec. Compressed encodings are in [Compressed](09-compressed.md).

### Assembler API

```
int brad_as_assemble(const char *source, struct brad_as_output *out);
```

`struct brad_as_output` carries the emitted `code[]`, the `entry_point`, a symbol table (`name` + `addr`), and a line-level `error`. Assembly resolves labels, validates operand widths, and rejects malformed instructions.

## Reference emulator (brad_core_emu)

The emulator models whole core clusters and runs programs without synthesising the RTL.

| Function | Description |
|---|---|
| `brad_soc_emu_init(soc, clusters, phoenix, falcon)` | initialise a heterogeneous SoC |
| `brad_soc_emu_init_ex(…, kestrel, falcon_lite)` | include the additional core models |
| `brad_core_emu_init(core, model, entry_pc, sp)` | initialise one core |
| `brad_core_emu_load(soc, program, words, addr)` | load a program image |
| `brad_core_emu_step(soc, cluster, core)` | execute one instruction |
| `brad_core_emu_run(soc, cluster, core, max_cycles)` | run one core |
| `brad_core_emu_run_all(soc, max_cycles)` | run every core |
| `brad_core_read_reg` / `brad_core_write_reg` | architectural register access |
| `brad_core_read_msr` / `brad_core_write_msr` | MSR access |
| `brad_core_emu_disasm(insn, buf, size)` | disassemble one instruction |

The core state tracks the pipeline model (Falcon-Lite, Falcon, Kestrel, Phoenix), stall counters, branch statistics, cache hits/misses, compressed mode, and the MSR set documented in [MSR reference](07-msr-reference.md).

## Scope and authority

- These are **reference implementations**, not tape-out silicon.
- The emulator is cycle-approximate; it is a software model, not a performance claim.
- The normative specification is the LaTeX source under `spec/`. Where a guide or model disagrees with it, the spec wins.

See also: [FPGA](12-fpga.md) for simulating the RTL, and the [BradVector repository](https://github.com/BradDevicesOfficial/BradVector) for the separate GPU/accelerator ISA and its shipped toolchain.
