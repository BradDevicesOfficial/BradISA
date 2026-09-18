# BradISA Documentation

The official programmer-visible reference for the Brad Devices instruction-set architecture.

## Repository layout

```
spec/
  bradisa_spec.tex          V1 base ISA (normative)
  bradisa_v2_ext.tex        V2 extensions — compressed, Kestrel, Falcon-Lite, VSET, power (normative)
rtl/
  verilog/                  Verilog-2001 RTL
    bradisa_defines.v       opcode and constant package
    brad_regfile.v          16 × 32-bit register file
    brad_alu.v              ALU (ADD/SUB/MUL/AND/OR/XOR/SHL/SHR)
    brad_core.v             Falcon 5-stage top level
    tb_brad_core.v          testbench (counts 1..100 in a loop)
    falcon_lite_core.v      Falcon-Lite (3-stage) reference
    kestrel_core.v          Kestrel (8-stage) reference
  vhdl/                     VHDL-2008 RTL (same design)
  fpga/                     Artix-7 / Cyclone V constraints + build scripts
docs/
  00-index.md               you are here
  01-quickstart.md          build the toolchain, write your first program
  02-data-model.md          words, bytes, address space, alignment, endianness
  03-instruction-formats.md five encoding families (RRR / RI / BR / JMP / RET)
  04-base-isa.md            complete V1 opcode reference — 16 instructions
  05-abi.md                 calling convention, register map, stack discipline
  06-exceptions.md          vector table, cause codes, trap flow
  07-msr-reference.md       model-specific registers — V1 and V2
  08-vector.md              VSET extension — full programmer's reference
  09-compressed.md          16-bit compressed instruction encoding
  10-power.md               power management — C-states, DVFS, perf counters
  11-toolchain.md           bradc, BVRT, bradgdb, braddev, BradTimeline, bradlib
  12-fpga.md                Falcon V1 RTL: pipeline, hazard model, synthesis
  13-gen2.md                SVEXT and future extensions
  GLOSSARY.md               every term, one line each
```

## What is BradISA

BradISA is the scalar instruction-set architecture shared by every Brad Devices CPU core: Falcon (efficiency-class), Kestrel (mid-range), Phoenix (performance-class), Phoenix-C (compact). It defines 16 general-purpose integer registers, 16 base opcodes, and a flat 32-bit byte-addressable address space.

V2 adds four extensions:

| Extension | Summary |
|---|---|
| Compressed | 16-bit instruction encoding — 30–40% code-size reduction |
| VSET | Fixed 32×256-bit vector / SIMD registers, FP and integer lanes |
| Power management | C-states, DVFS, per-unit clock-gating, PMRs |
| Kestrel + Falcon-Lite | new core models that consume the extensions |

V2 is fully backwards-compatible with V1 binaries.

## Authority

The normative specifications are:

- `spec/bradisa_spec.tex` — V1 base ISA, version 1.2
- `spec/bradisa_v2_ext.tex` — V2 extensions, version 2.2

In case of conflict, the LaTeX source takes precedence over these markdown documents. Where the markdown diverges, the spec is the truth.

## Reading order

New to BradISA? Start here:

1. [Quick-start](01-quickstart.md) — build the toolchain and run a first program
2. [Data model](02-data-model.md) — how memory and data are organised
3. [Base ISA](04-base-isa.md) — the 16 instructions, one by one
4. [ABI](05-abi.md) — calling convention and register roles

Already familiar? Jump to the reference:

- [Instruction formats](03-instruction-formats.md) — encoding bit-layouts
- [Exceptions](06-exceptions.md) — trap table and cause codes
- [MSR reference](07-msr-reference.md) — every model-specific register
- [VSET](08-vector.md) — vector / SIMD extension
- [Compressed](09-compressed.md) — 16-bit instructions
- [Power](10-power.md) — C-states, DVFS, performance counters
- [Toolchain](11-toolchain.md) — every developer-facing tool
- [FPGA](12-fpga.md) — synthesising the Falcon RTL core

## Cross-reference to existing docs

The following long-form documents in `docs/` are also part of this release and complement the numbered guides above:

- `ARCHITECTURE_GUIDE.md` — Brad ISA architecture guide for developers (extended V1 + compressed + VSET)
- `ARCHITECTURE_REFERENCE_MODEL.md` — full ARM-style architecture reference model
- `ISA_QUICK_REFERENCE.md` — one-page V1 + V2 instruction summary
- `TOOLCHAIN_GUIDE.md` — developer-facing toolchain reference

## Targets

| Core | Pipeline | Issue | IPC | DMIPS/MHz | Extensions | Target |
|---|---|---|---|---|---|---|
| Falcon-Lite | 3-stage | 1 | 0.55/0.70 | ~1.8 | — | ultra-efficient MCU |
| Falcon | 5-stage | 1 | 0.9/1.0 | 2.2 | compressed, VSET | efficiency-class |
| Kestrel | 8-stage | 2 | 1.2/1.8 | 3.0 | compressed, VSET, power | mid-range |
| Phoenix-C | 10-stage | 8 | 2.2/5.0 | 5.5 | all | compact performance |
| Phoenix-W | 10-stage | 8 | 2.2/5.0 | 5.5 | all | workstation-class |

All cores share the same ISA. Binary compatibility is absolute.

## Normative references

- `bradisa_spec.tex` V1.2 — base ISA
- `bradisa_v2_ext.tex` V2.2 — extensions
- `rtl/verilog/brad_core.v` — Falcon V1 Verilog (golden reference for structural tests)
- `site/llms.txt` — machine-readable product & architecture summary

## Licence

ISA documentation and RTL are released under the [MIT licence](../LICENSE).
