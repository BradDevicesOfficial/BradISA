# BradISA — Architecture Reference Model (ARM)

> *The authoritative architectural model for the Brad ISA family.*
> *Scope: BradISA V1 (Kinetic Gen1), V2 (extensions), and the intent for
> Gen2 (SVEXT). Defines what the architecture IS — the functional contract
> software is compiled against — independent of any one core implementation.*

---

## 1. Principles

1. **RISC-like fixed-width base**: 32-bit instructions, fixed register file,
   load/store memory model. Predictable decode, dense enough for a
   low-cost vertical stack.
2. **Asymmetric P/E cores, one ISA**: Phoenix (OoO) and Falcon (in-order)
   execute the *same* ISA; only performance and power differ. Software is
   not core-model-specific.
3. **Vector is first-class**: floating point lives in the vector register
   file (VSET), not scalar FP registers. The scalar core is an integer +,
   control machine; all FP/ML/DSP is vector work.
4. **Explicit privilege**: two privilege levels (Supervisor, Machine) with a
   vectored exception model and MSR-based control. No unprivileged
   architecture state tomfoolery.
5. **One fabric**: the BradFusion Fabric is the architectural interconnect —
   cores, NPU, memory controller, and GPU engines all hang off it. ISA-visible
   ordering and memory semantics are defined against the fabric.
6. **Honesty**: each capability is tagged [Gen1 (ships)] / [Gen2 (planned)] /
   [Vision (speculative)]. Nothing is claimed as real before it is.

---

## 2. Architectural State (Unprivileged)

| State | Count/Width | Notes |
|-------|-------------|-------|
| **GPRs r0–r15** | 16 × 32-bit (Gen1); 64-bit extendable | r0 hardwired zero; r1–r4 args (r1 = return); r13 sp, r14 lr, r15 pc (read-only); ABI table in the ARM appx. |
| **PC** | 32-bit | 4-byte aligned; BR ±128 KiB (16-bit word offset); JMP/CALL ±4 MiB (20-bit word offset); RET through lr |
| **Vector regs v0–v31** | 32 × 256-bit (VSET) | v0 = mask (Gen2 predication); ABI v1–v4 args, v8–v15 callee-saved |
| **Status MSRs** | VSTATUS.VE, POWER, etc. | Vector-enable, power/thermal |
| **Float** | none (scalar) | All FP in vector regs, lane 0 |

## 3. Privilege & Exceptions

| Level | Purpose | Entry |
|-------|---------|-------|
| **Machine (M)** | reset, SBI/firmware, secure monitoring (BradSec) | vector table @ fixed addresses |
| **Supervisor (S)** | BradOS kernel | MTVEC-style vectored table |

Vectored exception table (matching `bradisa_spec.tex`):

| Vector | Exception | Cause |
|--------|-----------|-------|
| 0x00 | reset / cold boot | — |
| 0x04 | undefined instruction | 1 |
| 0x08 | page fault | 2 |
| 0x0C | unaligned access | 3 |
| 0x10 | privilege violation | 4 |
| 0x14 | system call (ECALL) | 5 |
| 0x18 | timer interrupt | 6 |
| 0x20–0x2C | external IRQ 0–3 | 7 |

## 4. ISA Extensions

| Extension | Status | Defines |
|-----------|--------|---------|
| **Base V1** | [Gen1] | P/E-core scalar, control, load/store, ECALL/MRET |
| **Compressed (16-bit)** | [V2 spec; Gen2 hardware intent] | Dense encoding, mixed 16/32-bit words (STATUS.C) |
| **VSET (Vector)** | [Gen1] | 256-bit fixed-length SIMD; all FP lives here |
| **SVEXT (Scaleable Vector)** | [Gen2, intent] | SVE-style variable-length (128–512-bit), VL-agnostic code |
| **BradVector GPU ISA** | [Gen1] | SIMT warp ISA for Torox G1 GPU engines (separate but fabric-coherent) |
| **BA-ISA** | [Gen1] | BradApex block+vector accelerator ISA |

## 5. ABI & Calling Convention (Summary)

- First 4 scalar args in r1–r4; return value in r1; rest on the stack.
- First 4 vector args in v1–v4; FP scalars in lane 0 of arg vectors.
- Callee-saved: r8–r12; v8–v15.
- vector register file saved/restored by kernel on context switch (VSTATUS.VE).

## 6. Relationship to Other Specs

| Doc | Role |
|-----|------|
| `bradisa_spec.tex` | Base ISA V1 normative spec (encoding, exceptions, pipeline models) |
| `bradisa_v2_ext.tex` | V2 extensions: compressed, kestrel, VSET, power, branch predictor |
| `ARCHITECTURE_GUIDE.md` | Developer-oriented guide |
| BradVector repository | The separate GPU / accelerator SIMT ISA |

---

*Normative sources: `spec/bradisa_spec.tex`, `spec/bradisa_v2_ext.tex`. This ARM is a companion model, not a replacement spec.*