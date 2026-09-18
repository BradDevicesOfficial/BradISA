# Glossary

Every term a BradISA reader will meet, one line each.

## Cores

| Term | Definition |
|---|---|
| **Falcon** | The efficiency-class BradISA core: 5-stage, single-issue, in-order; the ISA's reference implementation. |
| **Falcon-Lite** | The smallest core: 3-stage, one wide, registers r0–r7 only; no hardware MUL. |
| **Kestrel** | The mid-range core: 8-stage, dual-issue in-order; adds compressed, VSET, and power management. |
| **Phoenix** | The performance-class core: 10-stage, octa-issue out-of-order with a 128-entry ROB. |
| **Phoenix-C** | Phoenix in compact form: 10-stage, octa-issue, 5.5 DMIPS/MHz, low-area. |
| **Phoenix-W** | Phoenix workstation variant: 10-stage, octa-issue, 5.5 DMIPS/MHz, high-area, workstation-class. |

## ISA concepts

| Term | Definition |
|---|---|
| **AVR‑like / RISC** | BradISA is a load/store RISC: only LDW/STW touch memory; everything else is register-to-register. |
| **GPR** | General-purpose register. BradISA has 16: r0–r15. |
| **r0** | Hardwired zero register. Reads as 0, writes are discarded. |
| **r14 (LR)** | Link register. Written by CALL with the return address; read by RET. |
| **r15 (PC)** | Program counter; read-only in software. |
| **MSR** | Model-Specific Register. System-control registers accessed via MSRWR/MSRRD. |
| **VSET** | The vector extension: 32 × 256-bit vector registers; integer, FP, reductions, memory ops. |
| **SEW** | Selected Element Width in bits. VSET supports 8/16/32/64. |
| **VLEN** | Vector register length in bits. Gen1 fixed at 256; Gen2 variable (128/256/512). |
| **v0 (vm)** | The vector mask register. Bit i of v0 predicates element i. |
| **SIMD** | Single Instruction, Multiple Data. VSET processes SEW-wide lanes in parallel. |
| **Compressed** | The 16-bit encoding extension; 30–40% code-size reduction. |
| **CPI / IPC** | Cycles-per-instruction / instructions-per-cycle. Processor efficiency metric. |
| **DMIPS/MHz** | Dhrystone MIPS per clock, the classic IPC-throughput proxy. |

## Microarchitecture

| Term | Definition |
|---|---|
| **Pipeline** | The instruction-processing stages: F1→F2→D→EX→WB (Falcon). |
| **RAW hazard** | Read-After-Write: an instruction reads a register the previous instruction writes. Falcon stalls one cycle. |
| **Forwarding** | Passing a result directly from EX to the next instruction without a stall. Falcon does not implement it; Kestrel does. |
| **Byte fetch** | Fetching instructions a byte at a time (classic AVR/AVR-like IR): NOT used — all BradISA cores fetch whole 32-bit words (or 16-bit halfwords in compressed mode). |
| **OoO** | Out-of-order execution — Phoenix's octa-issue execution with a reorder buffer. |
| **ROB** | Reorder Buffer: Phoenix's 128-entry completion buffer that retires instructions in order. |
| **BTB** | Branch Target Buffer. Kestrel has a 16-entry BTB; the tournament predictor pairs a bimodal table with a gshare history. |

## Exceptions and privilege

| Term | Definition |
|---|---|
| **Vector table** | The 11-entry exception table at 0x00–0x2C, one handler address per cause. |
| **Cause** | The exception reason (reset, undef, page fault, unaligned, priv, syscall, timer, IRQ0–3). |
| **Supervisor mode** | Privileged execution state set by STATUS.SVC during traps. |
| **User mode** | Unprivileged state; MSR writes and RFE trap as cause 0x10. |
| **EPC** | MSR 2, the address of the faulting instruction. |
| **EAR** | MSR 3, the faulting memory address (page fault / unaligned). |

## Power

| Term | Definition |
|---|---|
| **C-state** | Idle depth. C0 active → C3 deep sleep. |
| **DVFS** | Dynamic Voltage and Frequency Scaling. Governor moves (V, f) together. |
| **PM_CTRL** | MSR 8; initiates power-state transitions. |
| **CG_MASK** | MSR 21; per-pipeline-stage clock-gating enable. |
| **PWR_CAP** | MSR 14; maximum power envelope the governor must respect. |

## Toolchain

| Term | Definition |
|---|---|
| **bradasm** | Reference assembler: source text → instruction words + symbol table + entry point. |
| **brad_core_emu** | Reference cycle-approximate core / SoC emulator. |
| **Assembly source** | Plain text consumed by the reference assembler; the ISA does not fix a file extension. |
| **Compressed encoding** | 16-bit instruction forms, enabled with `.compress`. |
| **Instruction word** | One 32-bit (or 16-bit compressed) encoded instruction. |

## Ecosystem

| Term | Definition |
|---|---|
| **Brad Device** | The maker: Brad Devices (a small team located on Planet Earth + Mars). |
| **BradCore** | The CPU brand (BradISA cores). |
| **BradVector** | The GPU/accelerator ISA — dual-issue SIMT, 32-thread warps. Separate from BradISA. |
| **Torox** | The GPU die family: BFT100 (integrated), BGT100 (dedicated), BAT100 (datacenter). |
| **BradFusion** | The fabric tying CPU, GPU, NPU, memory together. BradISA and BradVector meet here — sharing memory, not instructions. |
| **SPMP** | Shared/Multi-Ported Memory pool. Unified memory: CPU + GPU + NPU. |
| **BradRAM / L4** | On-die SRAM last-level cache (Brad Vector: 32 MB Gen1). |
| **BradOS** | Brad Devices' OS, ported to every core. |
| **SVEXT** | The Gen2 scalable-vector extension (scoping only — VISION). |
| **Brad Silicon** | The private package holding production silicon designs; chips are not public. |

## Encoding

| Term | Definition |
|---|---|
| **RRR** | The register-register-register format: `OPCODE RD RS1 RS2 0000…`. |
| **RI** | The register-immediate format: `OPCODE RD RS1 IMM16`. |
| **BR** | The branch format: `OPCODE 0000 RS1 0000 OFFSET16`. |
| **JMP** | PC-relative 20-bit offset jump (±4 MiB). |
| **RET** | Fixed-encoding return: `0xF0000000`, PC ← LR. |
| **CRRR / CRI / CBR / CMV / CNOP / CRET** | The compressed 16-bit formats. |
| **Opcode packing** | 4-bit opcode in bits [31:28]; no aliases; each opcode maps to exactly one format. |
| **Immediate sign-extension** | 16-bit immediates are sign-extended to 32 before the operation. |
| **Barrel shifter** | SHL/SHR shift up to 31 places in one cycle; amount from rs2[4:0]. |
| **Dot product (V.DOT)** | A single-instruction Σ(a[i]·b[i]) over 8 lanes. |
| **Word alignment** | LDW/STW addresses must be 4-byte aligned; misalignment raises cause 0x02. |

## Quality

| Term | Definition |
|---|---|
| **SOLID** | The ISA quality tag: spec + docs + passing tests + RTL. |
| **SHIPPED** | The artifact exists and runs today. |
| **VISION** | Gen2 and beyond: scoped but not promised. |
| **Golden reference** | The normative spec; structural tests compare the RTL against it. |
| **Round-trip test** | Assemble then decode and compare — encoding correctness. |