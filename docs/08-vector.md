# VSET — Vector / SIMD Extension Reference

VSET is the BradISA vector extension: 32 × 256-bit vector registers, integer and floating-point SIMD, and a predicated load/store model. It shares opcode `0xF` with `RET` via a class-selector field and is enabled by `STATUS.VE` (MSR 23, bit 0).

## Overview

| Property | Value |
|---|---|
| Vector registers | v0–v31 (32 total) |
| VLEN | 256 bits (fixed, Gen1) |
| SEW options | 8, 16, 32, 64 bits |
| Elements per vector | 32 / 16 / 8 / 4 (respectively) |
| FP support | FP32 (Gen1), FP64 (Gen1, limited), FP16 (Gen2) |
| Mask register | v0 (bit 0 per SEW-byte) |
| Enable bit | `STATUS.VE` (MSR 23, bit 0) |
| Opcode | `0xF` (shared with RET) |

VSET instructions are encoded under opcode `0xF` using a 3-bit class field (`OPCODE_FLD[27:25]`). Each class contains its own sub-opcodes and format.

## Enabling VSET

Before executing any VSET instruction, the OS must set `STATUS.VE`:

```asm
MSRSET r0, STATUS     ; set bit 0 → VSTATUS.VE = 1
```

If `VE = 0` and a VSET opcode is encountered, cause `0x10` (privileged instruction) is raised.

## Register conventions

| Register | Name | Role | Saved by |
|---|---|---|---|
| v0 | vm | mask / predicate | caller |
| v1 | va0 | argument 0 / return value | caller |
| v2 | va1 | argument 1 | caller |
| v3 | va2 | argument 2 | caller |
| v4 | va3 | argument 3 | caller |
| v5–v7 | vt0–vt2 | scratch 0–2 | caller |
| v8–v15 | vs0–vs7 | saved 0–7 | **callee** |
| v16–v31 | vt3–vt18 | scratch 3–18 | caller |

See [ABI](05-abi.md) for the full vector calling convention.

## Vector element ordering

Elements are stored in little-endian order within the 256-bit register. For SEW = 32 (8 elements):

```
bits [31:0]    = element 0
bits [63:32]   = element 1
bits [95:64]   = element 2
...
bits [255:224] = element 7
```

For SEW = 8 (32 elements): each byte is one element, starting at bit 0.

## Predication (masking)

`v0` (vm) is the mask register. For masked operations, bit `i` of `v0` controls whether element `i` is updated:

- `v0[i] = 1`: element `i` is computed and stored
- `v0[i] = 0`: element `i` retains its previous value (for arithmetic) or is skipped (for loads)

The mask bit corresponds to the element index, not the byte offset. For SEW = 32, `v0[0]` masks element 0 (bits [31:0]), `v0[1]` masks element 1 (bits [63:32]), etc.

`v0[0]` also acts as a scalar flag for certain reduction operations.

## Instruction formats

All VSET instructions use opcode `0xF`. The 3-bit class field `OPCODE_FLD[27:25]` selects the instruction group:

| Class | `OPCODE_FLD[27:25]` | Description |
|---|---|---|
| 000 | VALU | vector integer arithmetic / logic |
| 001 | VFP | vector floating-point arithmetic |
| 010 | VMEM | vector load / store |
| 011 | VRED | vector reductions |
| 100 | VCVT | vector conversions (int↔float) |
| 101 | VSPL | vector splat / broadcast / insert |
| 110 | VSC | vector special / control |
| 111 | reserved | — |

Each class uses one of four sub-formats:

### VV format (vector-vector)

```
 31  28 27  25 24  20 19  15 14  10 9   8 7        0
+------+------+------+------+------+-----+-----------+
| 0xF  |CLASS |  VD  | VS1  | VS2  | SEW |  FUNCT    |
| [4]  | [3]  | [5]  | [5]  | [5]  | [2] |  [8]      |
+------+------+------+------+------+-----+-----------+
```

### VM format (vector-memory)

```
 31  28 27  25 24  20 19  16 15  13 12  11 10       0
+------+------+------+------+-----+-----+------------+
| 0xF  |CLASS |  VD  | BASE | MOP | SEW |  IMM       |
| [4]  | [3]  | [5]  | [4]  | [3] | [2] |  [11]      |
+------+------+------+------+-----+-----+------------+
```

### VX format (vector-scalar / vector-X)

```
 31  28 27  25 24  20 19  15 14  11 10  9 8        0
+------+------+------+------+------+-----+-----------+
| 0xF  |CLASS |  VD  | VS1  |  RS  | SEW |  FUNCT    |
| [4]  | [3]  | [5]  | [5]  | [4]  | [2] |  [9]      |
+------+------+------+------+------+-----+-----------+
```

### VSC format (vector-special-control)

```
 31  28 27  25 24  21 20  16 15                0
+------+------+------+------+-------------------+
| 0xF  |CLASS |  RD  | VS1  |  FUNCT / reserved |
| [4]  | [3]  | [4]  | [5]  |  [16]              |
+------+------+------+------+-------------------+
```

---

## Class 000 — VALU (Vector Arithmetic / Logic)

Integer and logic operations on vector registers.

### VV instructions (vector-vector)

| Funct | Mnemonic | Operation | SEW |
|---|---|---|---|
| 0x01 | V.ADD | vd[i] = vs1[i] + vs2[i] | 8/16/32 |
| 0x02 | V.SUB | vd[i] = vs1[i] − vs2[i] | 8/16/32 |
| 0x03 | V.AND | vd[i] = vs1[i] & vs2[i] | 8/16/32 |
| 0x04 | V.OR | vd[i] = vs1[i] \| vs2[i] | 8/16/32 |
| 0x05 | V.XOR | vd[i] = vs1[i] ^ vs2[i] | 8/16/32 |
| 0x06 | V.SLL | vd[i] = vs1[i] << vs2[i][4:0] | 16/32 |
| 0x07 | V.SRL | vd[i] = vs1[i] >> vs2[i][4:0] (logical) | 16/32 |
| 0x08 | V.MUL | vd[i] = low32(vs1[i] × vs2[i]) | 16/32 |
| 0x09 | V.CMP.EQ | vd[i] = (vs1[i] == vs2[i]) ? 0xFFFFFFFF : 0 | 32 |
| 0x0A | V.CMP.LT | vd[i] = (vs1[i] < vs2[i]) ? 0xFFFFFFFF : 0 (unsigned) | 32 |
| 0x0B | V.MIN | vd[i] = min(vs1[i], vs2[i]) (unsigned) | 32 |
| 0x0C | V.MAX | vd[i] = max(vs1[i], vs2[i]) (unsigned) | 32 |
| 0x0D | V.CMP.GT | vd[i] = (vs1[i] > vs2[i]) ? 0xFFFFFFFF : 0 (unsigned) | 32 |
| 0x0E | V.MOV | vd = vs1 (register move) | — |

All VV operations are predicated by `v0[i]`.

### VX instructions (vector-scalar)

| Funct | Mnemonic | Operation | SEW |
|---|---|---|---|
| 0x01 | V.ADD.S | vd[i] = vs1[i] + rs | 32 |
| 0x02 | V.SUB.S | vd[i] = vs1[i] − rs | 32 |
| 0x03 | V.MUL.S | vd[i] = vs1[i] × rs | 32 |
| 0x04 | V.AND.S | vd[i] = vs1[i] & rs | 32 |
| 0x05 | V.OR.S | vd[i] = vs1[i] \| rs | 32 |
| 0x06 | V.XOR.S | vd[i] = vs1[i] ^ rs | 32 |
| 0x07 | V.SRA | vd[i] = vs1[i] >> rs[4:0] (arithmetic) | 32 |

`rs` is a scalar GPR. The scalar value is broadcast to all active lanes.

---

## Class 001 — VFP (Vector Floating-Point)

Floating-point operations on vector registers. Requires `VSTATUS.VFP = 1` (MSR 23, bit 2).

| Funct | Mnemonic | Operation | SEW |
|---|---|---|---|
| 0x01 | V.FADD | vd[i] = vs1[i] + vs2[i] | 32 |
| 0x02 | V.FSUB | vd[i] = vs1[i] − vs2[i] | 32 |
| 0x03 | V.FMUL | vd[i] = vs1[i] × vs2[i] | 32 |
| 0x04 | V.FDIV | vd[i] = vs1[i] / vs2[i] | 32 |
| 0x05 | V.FMIN | vd[i] = min(vs1[i], vs2[i]) | 32 |
| 0x06 | V.FMAX | vd[i] = max(vs1[i], vs2[i]) | 32 |
| 0x07 | V.FMADD | vd[i] = vs1[i] × vs2[i] + vd[i] (fused multiply-add) | 32 |
| 0x08 | V.FMSUB | vd[i] = vs1[i] × vs2[i] − vd[i] (fused multiply-sub) | 32 |
| 0x09 | V.FABS | vd[i] = abs(vs1[i]) | 32 |
| 0x0A | V.FNEG | vd[i] = −vs1[i] | 32 |
| 0x0B | V.FSQRT | vd[i] = sqrt(vs1[i]) | 32 |

FP16 support is Gen2-only (requires `VSTATUS.VFP = 2` to activate half-precision mode). FP64 support is partial in Gen1 (VADD/VMUL only) and full in Gen2.

---

## Class 010 — VMEM (Vector Memory)

Vector load and store operations. Elements are loaded/stored sequentially from a base address in a scalar GPR.

| MOP | Mnemonic | Operation | SEW |
|---|---|---|---|
| 000 | V.LW | load vector word: vd[i] = mem32[base + i×SEW/8] | 8/16/32 |
| 001 | V.SW | store vector word: mem32[base + i×SEW/8] = vs1[i] | 8/16/32 |
| 010 | V.LWG | (Gen2 reserved) gather load | — |
| 011 | V.SWG | (Gen2 reserved) scatter store | — |
| 100 | V.LDI | load vector immediate: vd[i] = imm (broadcast) | 32 |
| 101 | V.FILL | fill all lanes: vd[i] = rs (scalar broadcast) | 32 |

### Load addressing

```
effective_address = base_rs + i × (SEW / 8)
```

where `i` is the element index (0 to VLEN/SEW − 1). All elements must be naturally aligned to their SEW boundary.

### Masking on loads

If `v0[i] = 0`, element `i` is not loaded — the register retains its previous value for that lane. This enables partial vector updates.

### Masking on stores

If `v0[i] = 0`, element `i` is not stored — memory is unaffected for that lane.

### V.LDI / V.FILL

`V.LDI` loads an immediate value into all lanes (unmasked). `V.FILL` broadcasts a scalar GPR to all lanes. Both are used to initialise vectors before computation.

---

## Class 011 — VRED (Vector Reductions)

Reduction operations produce a scalar result from a vector. The scalar result is placed in `v1[0]` (the low 32 bits of `va0`).

| Funct | Mnemonic | Operation | SEW |
|---|---|---|---|
| 0x01 | V.SUM | v1[0] = Σ vs1[i] (integer sum) | 32 |
| 0x02 | V.FSUM | v1[0] = Σ vs1[i] (FP sum) | 32 |
| 0x03 | V.MAX | v1[0] = max(vs1[i]) (unsigned integer) | 32 |
| 0x04 | V.MIN | v1[0] = min(vs1[i]) (unsigned integer) | 32 |
| 0x05 | V.FMAX | v1[0] = max(vs1[i]) (FP) | 32 |
| 0x06 | V.FMIN | v1[0] = min(vs1[i]) (FP) | 32 |
| 0x07 | V.DOT | v1[0] = Σ (vs1[i] × vs2[i]) (integer dot product) | 32 |
| 0x08 | V.FDOT | v1[0] = Σ (vs1[i] × vs2[i]) (FP dot product) | 32 |

`v0[0]` acts as a global enable: if `v0[0] = 0`, the reduction is a no-op. Individual lane masking does not apply to reductions — it is all-or-nothing via `v0[0]`.

### Dot product

The dot product accumulates `vs1[i] × vs2[i]` across all active lanes (all 8 for SEW=32) and stores the sum in `v1[0]`. This is a single-instruction dot product — no loop required.

---

## Class 100 — VCVT (Vector Conversions)

Convert between integer and floating-point representations, and between precision widths.

| Funct | Mnemonic | Operation | SEW |
|---|---|---|---|
| 0x01 | V.CVT.SF | vd[i] = float(vs1[i]) (int32 → float32) | 32 |
| 0x02 | V.CVT.SI | vd[i] = int(vs1[i]) (float32 → int32, trunc) | 32 |
| 0x03 | V.CVT.HF | vd[i] = half(vs1[i]) (float32 → float16, Gen2) | 32 |
| 0x04 | V.CVT.SF.HF | vd[i] = float(vs1[i]) (float16 → float32, Gen2) | 16 |
| 0x05 | V.CVT.WFx | vd[i] = widen(vs1[i]) (int16 → int32) | 16 |
| 0x06 | V.NARROW | vd[i] = narrow(vs1[i]) (int32 → int16, saturate) | 32 |

---

## Class 101 — VSPL (Vector Splat / Broadcast / Insert)

Broadcast scalars to all lanes, or insert a single element.

| Funct | Mnemonic | Operation | SEW |
|---|---|---|---|
| 0x01 | V.BCAST | vd[i] = rs for all i (broadcast scalar) | 32 |
| 0x02 | V.ADDX | vd[0] = vs1[0] + rs (scalar add to element 0) | 32 |
| 0x03 | V.SUBX | vd[0] = vs1[0] − rs | 32 |
| 0x04 | V.MULX | vd[0] = vs1[0] × rs | 32 |
| 0x05 | V.FADDX | vd[0] = vs1[0] + frs (FP scalar add, Gen2) | 32 |
| 0x06 | V.FMULX | vd[0] = vs1[0] × frs (FP scalar mul, Gen2) | 32 |
| 0x07 | V.INS | vd[element_idx] = rs (insert one element) | 32 |

`V.BCAST` is the primary way to initialise a vector from a scalar. `V.INS` inserts a single element at the index specified by a second scalar register.

---

## Class 110 — VSC (Vector Special / Control)

Special-purpose vector instructions.

| Funct | Mnemonic | Operation | Description |
|---|---|---|---|
| 0x01 | V.EXT | vd = vs1 (register extract / move) | move between vector regs |

---

## Quick reference — all VSET instructions

| Class | Funct | Mnemonic | Format | Description |
|---|---|---|---|---|
| 000 | 0x01 | V.ADD | VV | vector add |
| 000 | 0x02 | V.SUB | VV | vector subtract |
| 000 | 0x03 | V.AND | VV | vector AND |
| 000 | 0x04 | V.OR | VV | vector OR |
| 000 | 0x05 | V.XOR | VV | vector XOR |
| 000 | 0x06 | V.SLL | VV | vector shift left |
| 000 | 0x07 | V.SRL | VV | vector shift right (logical) |
| 000 | 0x08 | V.MUL | VV | vector multiply (low 32) |
| 000 | 0x09 | V.CMP.EQ | VV | vector compare equal |
| 000 | 0x0A | V.CMP.LT | VV | vector compare less-than |
| 000 | 0x0B | V.MIN | VV | vector min |
| 000 | 0x0C | V.MAX | VV | vector max |
| 000 | 0x0D | V.CMP.GT | VV | vector compare greater-than |
| 000 | 0x0E | V.MOV | VV | vector register move |
| 000 | 0x01 | V.ADD.S | VX | vector + scalar |
| 000 | 0x02 | V.SUB.S | VX | vector − scalar |
| 000 | 0x03 | V.MUL.S | VX | vector × scalar |
| 000 | 0x04 | V.AND.S | VX | vector AND scalar |
| 000 | 0x05 | V.OR.S | VX | vector OR scalar |
| 000 | 0x06 | V.XOR.S | VX | vector XOR scalar |
| 000 | 0x07 | V.SRA | VX | vector shift right arithmetic |
| 001 | 0x01 | V.FADD | VV | FP vector add |
| 001 | 0x02 | V.FSUB | VV | FP vector subtract |
| 001 | 0x03 | V.FMUL | VV | FP vector multiply |
| 001 | 0x04 | V.FDIV | VV | FP vector divide |
| 001 | 0x05 | V.FMIN | VV | FP vector min |
| 001 | 0x06 | V.FMAX | VV | FP vector max |
| 001 | 0x07 | V.FMADD | VV | FP fused multiply-add |
| 001 | 0x08 | V.FMSUB | VV | FP fused multiply-sub |
| 001 | 0x09 | V.FABS | VV | FP absolute value |
| 001 | 0x0A | V.FNEG | VV | FP negate |
| 001 | 0x0B | V.FSQRT | VV | FP square root |
| 010 | 000 | V.LW | VM | load vector |
| 010 | 001 | V.SW | VM | store vector |
| 010 | 100 | V.LDI | VM | load vector immediate |
| 010 | 101 | V.FILL | VM | broadcast scalar to vector |
| 011 | 0x01 | V.SUM | VRED | integer sum reduction |
| 011 | 0x02 | V.FSUM | VRED | FP sum reduction |
| 011 | 0x03 | V.MAX | VRED | integer max reduction |
| 011 | 0x04 | V.MIN | VRED | integer min reduction |
| 011 | 0x05 | V.FMAX | VRED | FP max reduction |
| 011 | 0x06 | V.FMIN | VRED | FP min reduction |
| 011 | 0x07 | V.DOT | VRED | integer dot product |
| 011 | 0x08 | V.FDOT | VRED | FP dot product |
| 100 | 0x01 | V.CVT.SF | VCVT | int32 → float32 |
| 100 | 0x02 | V.CVT.SI | VCVT | float32 → int32 |
| 100 | 0x03 | V.CVT.HF | VCVT | float32 → float16 (Gen2) |
| 100 | 0x04 | V.CVT.SF.HF | VCVT | float16 → float32 (Gen2) |
| 100 | 0x05 | V.CVT.WFx | VCVT | widen int16 → int32 |
| 100 | 0x06 | V.NARROW | VCVT | narrow int32 → int16 |
| 101 | 0x01 | V.BCAST | VSPL | broadcast scalar |
| 101 | 0x02 | V.ADDX | VSPL | scalar add to elem 0 |
| 101 | 0x03 | V.SUBX | VSPL | scalar sub from elem 0 |
| 101 | 0x04 | V.MULX | VSPL | scalar mul elem 0 |
| 101 | 0x05 | V.FADDX | VSPL | FP scalar add elem 0 (Gen2) |
| 101 | 0x06 | V.FMULX | VSPL | FP scalar mul elem 0 (Gen2) |
| 101 | 0x07 | V.INS | VSPL | insert one element |
| 110 | 0x01 | V.EXT | VSC | vector register move |

---

## Performance notes

- **Vector ALU latency:** 1 cycle per element (8 cycles for a full 256-bit VADD at SEW=32 on Phoenix)
- **Vector load/store:** 1 cycle per element, pipelined
- **Dot product:** 2 cycles (multiply + accumulate)
- **FP square root:** 4 cycles (iterative, shared with integer MUL unit)

Vector operations exploit instruction-level parallelism — multiple vector instructions can be in-flight simultaneously on Phoenix (octa-issue).

## Enabling VSET for user processes

The OS must set `STATUS.VE = 1` before entering user mode if the process uses VSET instructions. If `VE = 0`, any VSET opcode raises cause `0x10`. This allows the OS to:
- Provide a software emulation path for VSET on cores that lack hardware
- Disable vector for processes that don't need it (power savings)
- Provide a VSET trap for profiling / instruction emulation

## Relationship to BradVector (GPU)

VSET is the **CPU-side** vector / SIMD extension. It runs on BradISA scalar cores (Falcon, Kestrel, Phoenix). The GPU uses **BradVector** — a separate SIMT ISA with 32-thread warps, shader kernels, and compute lanes. VSET and BradVector share no instructions, no registers, and no execution units. They meet at the BradFusion fabric and SPMP memory, not at the ISA level.
