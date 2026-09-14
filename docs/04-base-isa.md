# Base ISA — V1 Opcode Reference

All 16 BradISA V1 instructions. Each entry gives the encoding, semantic operation, flag effects, and an example.

See [Instruction Formats](03-instruction-formats.md) for the bit-layout families (RRR, RI, BR, JMP, RET).

## Conventions

| Notation | Meaning |
|---|---|
| `rs1`, `rs2` | source register index (4 bits, 0–15) |
| `rd` | destination register index (4 bits, 0–15) |
| `imm16` | 16-bit signed immediate |
| `imm20` | 20-bit signed immediate (JMP only) |
| `sext(n)` | sign-extend `n` to 32 bits |
| `mem32[addr]` | 32-bit word read from address `addr` |
| `PC` | address of the current instruction |
| `LR` | link register, alias `r14` |

## 0x0 — ADD

| | |
|---|---|
| **Format** | RRR |
| **Encoding** | `OPCODE[31:28]=0x0  RD[27:24]  RS1[23:20]  RS2[19:16]  0000…0000[15:0]` |
| **Operation** | `rd ← rs1 + rs2` |
| **Flags** | none |
| **Cycles (Falcon)** | 1 |

Adds the values of `rs1` and `rs2`, stores the lower 32 bits of the result in `rd`. Carry is discarded.

```asm
ADD r1, r2, r3       ; r1 = r2 + r3
ADD r1, r0, r0       ; r1 = 0 (NOP variant)
```

## 0x1 — SUB

| | |
|---|---|
| **Format** | RRR |
| **Encoding** | `OPCODE[31:28]=0x1  RD[27:24]  RS1[23:20]  RS2[19:16]  0000…0000[15:0]` |
| **Operation** | `rd ← rs1 − rs2` |
| **Flags** | none |
| **Cycles (Falcon)** | 1 |

Subtracts `rs2` from `rs1`. The result is the two's-complement difference. No borrow flag is set.

```asm
SUB r1, r2, r3       ; r1 = r2 - r3
```

## 0x2 — MUL

| | |
|---|---|
| **Format** | RRR |
| **Encoding** | `OPCODE[31:28]=0x2  RD[27:24]  RS1[23:20]  RS2[19:16]  0000…0000[15:0]` |
| **Operation** | `rd ← rs1 × rs2[31:0]` (low 32 bits) |
| **Flags** | none |
| **Cycles (Falcon)** | 3 |

Produces the low 32 bits of the unsigned 32×32 product. High 32 bits are discarded. For a full 64-bit result, issue two `MUL` operations with appropriate shift/combine sequences.

Falcon-Lite does not implement `MUL` in hardware — it returns 0. See [toolchain](11-toolchain.md) for the software multiply sequence.

```asm
MUL r1, r2, r3       ; r1 = low32(r2 * r3)
```

## 0x3 — AND

| | |
|---|---|
| **Format** | RRR |
| **Encoding** | `OPCODE[31:28]=0x3  RD[27:24]  RS1[23:20]  RS2[19:16]  0000…0000[15:0]` |
| **Operation** | `rd ← rs1 & rs2` |
| **Flags** | none |
| **Cycles (Falcon)** | 1 |

Bitwise AND of `rs1` and `rs2`.

```asm
AND r1, r2, r3       ; r1 = r2 AND r3
AND r1, r1, 0xFF     ; mask to low byte (via ADDI first: ADDI r2,r0,0xFF)
```

## 0x4 — OR

| | |
|---|---|
| **Format** | RRR |
| **Encoding** | `OPCODE[31:28]=0x4  RD[27:24]  RS1[23:20]  RS2[19:16]  0000…0000[15:0]` |
| **Operation** | `rd ← rs1 \| rs2` |
| **Flags** | none |
| **Cycles (Falcon)** | 1 |

Bitwise OR of `rs1` and `rs2`.

```asm
OR r1, r2, r3        ; r1 = r2 OR r3
```

## 0x5 — XOR

| | |
|---|---|
| **Format** | RRR |
| **Encoding** | `OPCODE[31:28]=0x5  RD[27:24]  RS1[23:20]  RS2[19:16]  0000…0000[15:0]` |
| **Operation** | `rd ← rs1 ^ rs2` |
| **Flags** | none |
| **Cycles (Falcon)** | 1 |

Bitwise exclusive-OR. Also useful for zeroing a register: `XOR r1, r1, r1`.

```asm
XOR r1, r2, r3       ; r1 = r2 XOR r3
XOR r5, r5, r5       ; r5 = 0 (clear)
```

## 0x6 — SHL

| | |
|---|---|
| **Format** | RRR |
| **Encoding** | `OPCODE[31:28]=0x6  RD[27:24]  RS1[23:20]  RS2[19:16]  0000…0000[15:0]` |
| **Operation** | `rd ← rs1 << rs2[4:0]` |
| **Flags** | none |
| **Cycles (Falcon)** | 1 (barrel) |

Logical left shift. Only the low 5 bits of `rs2` are used as the shift amount (0–31). Vacated bits are zero-filled.

```asm
SHL r1, r2, r3       ; r1 = r2 << (r3 AND 0x1F)
SHL r1, r2, 3        ; r1 = r2 << 3 (via ADDI r3, r0, 3 first)
```

## 0x7 — SHR

| | |
|---|---|
| **Format** | RRR |
| **Encoding** | `OPCODE[31:28]=0x7  RD[27:24]  RS1[23:20]  RS2[19:16]  0000…0000[15:0]` |
| **Operation** | `rd ← rs1 >> rs2[4:0]` (logical) |
| **Flags** | none |
| **Cycles (Falcon)** | 1 (barrel) |

Logical right shift. Vacated bits are zero-filled. No arithmetic (sign-extending) shift exists; emulate with `SHR` + `AND` + conditional `OR` with `0x80000000` if the sign bit must be propagated.

```asm
SHR r1, r2, r3       ; r1 = r2 >> (r3 AND 0x1F) (unsigned)
```

## 0x8 — ADDI

| | |
|---|---|
| **Format** | RI |
| **Encoding** | `OPCODE[31:28]=0x8  RD[27:24]  RS1[23:20]  IMM16[15:0]` |
| **Operation** | `rd ← rs1 + sext(imm16)` |
| **Flags** | none |
| **Cycles (Falcon)** | 1 |

Add immediate. The 16-bit immediate is sign-extended to 32 bits before addition. Range: −32,768 to +32,767.

This is the most-used instruction — it serves as `LI` (load immediate) when `rs1 = r0`, as `MV` when `imm16 = 0`, and as `NOP` when `rd = rs1 = r0`.

```asm
ADDI r1, r0, 42      ; r1 = 42
ADDI r1, r1, -1      ; r1 = r1 - 1
ADDI r15, r0, 0      ; NOP (rd = r15 = PC = 0, then overwritten)
ADDI r0, r0, 0       ; canonical NOP
```

## 0x9 — LDW

| | |
|---|---|
| **Format** | RI |
| **Encoding** | `OPCODE[31:28]=0x9  RD[27:24]  RS1[23:20]  IMM16[15:0]` |
| **Operation** | `rd ← mem32[rs1 + sext(imm16)]` |
| **Flags** | none (page-fault on misalignment) |
| **Cycles (Falcon)** | 3 + forwarding |

Load word. Reads a 32-bit word from memory at address `rs1 + sext(imm16)` into `rd`. The address must be 4-byte aligned; otherwise a page-fault exception (`STATUS[0x08]`) is raised.

Address is computed as in `ADDI`: base + sign-extended 16-bit immediate. Range: ±32 KiB from the base register.

```asm
LDW r1, [r2, 0]      ; r1 = mem32[r2]
LDW r1, [r2, 4]      ; r1 = mem32[r2 + 4]
LDW r1, [r2, -8]     ; r1 = mem32[r2 - 8]
```

## 0xA — STW

| | |
|---|---|
| **Format** | RI |
| **Encoding** | `OPCODE[31:28]=0xA  RD[27:24]=rs2  RS1[23:20]  IMM16[15:0]` |
| **Operation** | `mem32[rs1 + sext(imm16)] ← rs2` |
| **Flags** | none (page-fault on misalignment) |
| **Cycles (Falcon)** | 1 |

Store word. Writes the 32-bit value of `rs2` to memory at address `rs1 + sext(imm16)`. The address must be 4-byte aligned.

In the encoding, the `RD` field holds the index of the source register (`rs2`). The assembler resolves this mapping automatically.

```asm
STW r1, [r2, 0]      ; mem32[r2] = r1
STW r1, [r2, 4]      ; mem32[r2 + 4] = r1
```

## 0xB — BZ

| | |
|---|---|
| **Format** | BR |
| **Encoding** | `OPCODE[31:28]=0xB  0000[27:24]  RS1[23:20]  0000[19:16]  OFFSET16[15:0]` |
| **Operation** | `if rs1 == 0: PC ← PC + 4 + sext(OFFSET16) × 4` |
| **Flags** | none |
| **Cycles (Falcon)** | 1 (taken: +2 flush) |

Branch if zero. Tests `rs1` for equality with zero. If the condition is true, the branch is taken and control transfers to the computed target. If false, execution continues with the next instruction.

The offset is a signed 16-bit word offset (not byte offset), multiplied by 4. Range: ±128 KiB.

```asm
BZ r1, skip          ; skip ahead if r1 == 0
```

## 0xC — BNZ

| | |
|---|---|
| **Format** | BR |
| **Encoding** | `OPCODE[31:28]=0xC  0000[27:24]  RS1[23:20]  0000[19:16]  OFFSET16[15:0]` |
| **Operation** | `if rs1 != 0: PC ← PC + 4 + sext(OFFSET16) × 4` |
| **Flags** | none |
| **Cycles (Falcon)** | 1 (taken: +2 flush) |

Branch if not-zero. The inverse of `BZ`.

```asm
BNZ r1, loop         ; loop while r1 != 0
```

### Loop example

```asm
  ADDI r1, r0, 10     ; counter = 10
loop:
  ADDI r1, r1, -1     ; counter--
  BNZ r1, loop        ; loop while counter != 0
  ; fall through when r1 == 0
```

## 0xD — JMP

| | |
|---|---|
| **Format** | JMP |
| **Encoding** | `OPCODE[31:28]=0xD  00000000[27:4]  OFFSET20[3:0]  OFFSET20[19:4]` — see note |
| **Operation** | `PC ← PC + 4 + sext(OFFSET20) × 4` |
| **Flags** | none |
| **Cycles (Falcon)** | 1 (always +2 flush) |

Unconditional jump. The 20-bit offset is sign-extended, multiplied by 4, and added to `PC + 4`. Range: ±4 MiB.

JMP does not save a return address. For subroutine calls, use `CALL`.

```asm
JMP 0x1000           ; jump to address 0x1000 (via offset calculation)
```

## 0xE — CALL

| | |
|---|---|
| **Format** | RI (specialised) |
| **Encoding** | `OPCODE[31:28]=0xE  RD[27:24]  RS1[23:20]  IMM16[15:0]` |
| **Operation** | `LR ← PC + 4;  PC ← PC + 4 + sext(IMM16) × 4` |
| **Flags** | none |
| **Cycles (Falcon)** | 1 (+2 flush) |

Call subroutine. Saves the return address (`PC + 4`) in `LR` (r14), then jumps to the target. The offset is encoded identically to `JMP` (20-bit word offset). The `RD` field selects the link register — conventionally `r14`.

On entry to the callee, `LR` holds the return address. Use `RET` to return.

```asm
CALL r14, subroutine  ; lr = pc+4, jump to subroutine
```

### Nested calls

For nested calls, the caller must save `LR` to the stack before issuing another `CALL`:

```asm
STW r14, [r13, -4]    ; save return address
ADDI r13, r13, -8     ; adjust stack pointer
CALL r14, subroutine  ; lr = pc+4 (overwrites old LR)
ADDI r13, r13, 8      ; restore stack
LDW r14, [r13, -4]    ; restore return address
```

## 0xF — RET

| | |
|---|---|
| **Format** | RET (fixed encoding) |
| **Encoding** | `0xF0000000` |
| **Operation** | `PC ← LR` |
| **Flags** | none |
| **Cycles (Falcon)** | 1 (+2 flush) |

Return from subroutine. Reads `LR` (r14) and sets the program counter. The fetch pipeline is flushed.

Opcode `0xF` is shared with the VSET vector extension via a class-selector field — see [VSET](08-vector.md). On a core without VSET, `0xF0000000` decodes as `RET`. On a core with VSET, `RET` is encoded with the vector class bits clear (`class = 000`, funct = 0x00) — the decoder routes correctly.

```asm
RET                  ; return to caller (PC ← LR)
```

## Instruction set summary

| Op | Mnemonic | Format | Operation | Cycles |
|---|---|---|---|---|
| 0x0 | ADD | RRR | rd ← rs1 + rs2 | 1 |
| 0x1 | SUB | RRR | rd ← rs1 − rs2 | 1 |
| 0x2 | MUL | RRR | rd ← low32(rs1 × rs2) | 3 |
| 0x3 | AND | RRR | rd ← rs1 & rs2 | 1 |
| 0x4 | OR | RRR | rd ← rs1 \| rs2 | 1 |
| 0x5 | XOR | RRR | rd ← rs1 ^ rs2 | 1 |
| 0x6 | SHL | RRR | rd ← rs1 << rs2[4:0] | 1 |
| 0x7 | SHR | RRR | rd ← rs1 >> rs2[4:0] | 1 |
| 0x8 | ADDI | RI | rd ← rs1 + sext(imm16) | 1 |
| 0x9 | LDW | RI | rd ← mem32[rs1+sext(imm16)] | 3+ |
| 0xA | STW | RI | mem32[rs1+sext(imm16)] ← rs2 | 1 |
| 0xB | BZ | BR | if rs1==0: branch | 1/3 |
| 0xC | BNZ | BR | if rs1!=0: branch | 1/3 |
| 0xD | JMP | JMP | PC ← PC+4+sext(off20)×4 | 3 |
| 0xE | CALL | RI | LR=PC+4; PC ← PC+4+sext(off)×4 | 3 |
| 0xF | RET | RET | PC ← LR | 3 |

Branch/jump costs: 1 cycle not-taken, 3 cycles taken (2-cycle pipeline flush on Falcon).
