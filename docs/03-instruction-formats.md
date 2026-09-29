# Instruction Formats

All BradISA instructions are 32 bits wide. V1 defines five encoding families: three register-register (RRR, RI), one branch (BR), and two control (JMP, RET). V2 adds a sixth family (compressed, 16-bit) — see [Compressed](09-compressed.md).

The instruction set contains 16 opcodes (`OPCODE[31:28]`), each mapped to exactly one encoding family. There is no opcode aliasing.

## RRR — Register-register-register

Used by: `ADD`, `SUB`, `MUL`, `AND`, `OR`, `XOR`, `SHL`, `SHR`

```
 31  28 27  24 23  20 19  16 15            0
+------+------+------+------+----------------+
| OPCODE|  RD  | RS1  | RS2  | 0000...0000   |
| [4]   | [4]  | [4]  | [4]  | [16]          |
+------+------+------+------+----------------+
```

Field widths (bits): `OPCODE` 4 · `RD` 4 · `RS1` 4 · `RS2` 4 · reserved 16

**Operation:** `RD ← RS1 OP RS2`

The reserved field must be zero. Assemblers shall emit `0x00000` in bits [15:0]. Decoders shall ignore bits [15:0].

## RI — Register-immediate

Used by: `ADDI`, `LDW`, `STW`

```
 31  28 27  24 23  20 19                               0
+------+------+------+------------------------------------+
| OPCODE|  RD  | RS1  | IMM16 (signed, sign-extended)     |
| [4]   | [4]  | [4]  | [16]                              |
+------+------+------+------------------------------------+
```

Field widths (bits): `OPCODE` 4 · `RD` 4 · `RS1` 4 · `IMM16` 16

**Operations:**

| Instruction | Operation |
|---|---|
| `ADDI rd, rs1, imm16` | `rd ← rs1 + sext(imm16)` |
| `LDW rd, [rs1 + imm16]` | `rd ← mem32[rs1 + sext(imm16)]` |
| `STW rs2, [rs1 + imm16]` | `mem32[rs1 + sext(imm16)] ← rs2` |

For `STW`, the destination register field (`RD`) holds the source register (`rs2`). The assembler resolves this mapping.

The 16-bit immediate is **sign-extended** to 32 bits before the operation. Range: −32,768 to +32,767.

## BR — Branch-relative

Used by: `BZ`, `BNZ`

```
 31  28 27  24 23  20 19  16 15                               0
+------+------+--------+------+---------------------------------+
| OPCODE| 0000|  RS1   | 0000 | OFFSET16 (signed, ×4)           |
| [4]   | [4] | [4]    | [4]  | [16]                            |
+------+------+--------+------+---------------------------------+
```

Field widths (bits): `OPCODE` 4 · reserved 4 · `RS1` 4 · reserved 4 · `OFFSET16` 16

**Operation:**

```
target = PC + 4 + sext(OFFSET16) × 4
```

The offset is a signed 16-bit word-count (not byte-count). It is sign-extended and multiplied by 4 before addition. Branch range: **±128 KiB** from the branch instruction.

`RD` and `RS2` fields are zero/reserved and ignored by the decoder.

| Instruction | Condition |
|---|---|
| `BZ rs1, offset` | branch if `rs1 == 0` |
| `BNZ rs1, offset` | branch if `rs1 != 0` |

No flags are set by ALU operations. Branch conditions test the register value directly.

## JMP — Jump

Used by: `JMP`

```
 31  28 27  24 23  20 19  16 15                0
+------+------+------+------+-------------------+
| OPCODE| 0000 | 0000 | 0000 | OFFSET16 (×4)     |
| [4]   | [4]  | [4]  | [4]  | [16]              |
+------+------+------+------+-------------------+
```

Field widths (bits): `OPCODE` 4 · unused 12 · `OFFSET16` 16

**Operation:**

```
target = PC + 4 + sext(OFFSET16) × 4
```

The 16-bit offset is sign-extended and multiplied by 4. Range: **±128 KiB** from the jump instruction — identical to `BZ` and `BNZ`. Anything wider needs a veneer.

`JMP` is unconditional. It is the only unconditional direct control transfer. `CALL` uses this same encoding; see [Base ISA](04-base-isa.md#0xe--call).

## RET — Return

Used by: `RET`

```
 31  28 27  24 23  20 19  16 15                0
+------+------+------+------+-------------------+
| 0xF  | 0000 | RS1  | 0000 | 0x0000          |
| [4]   | [4]  | [4]  | [4]  | [16]            |
+------+------+------+------+-------------------+
```

**Operation:** `PC ← rs1`

RET is not a fixed encoding: it is a register-indirect jump selected by opcode `0xF`, and it returns to whichever register `RS1` names. Naming the link register is the usual case:

```
RET          ; PC ← LR (r14)   →  0xF0E00000
```

`0xF0000000` is a *valid* RET that returns to `r0`, i.e. address 0 — it restarts the program from the top rather than returning to a caller. RET does not modify `LR`, and the processor flushes the fetch pipeline on it.

RET occupies opcode `0xF`. When the vector extension (VSET) is present, opcode `0xF` is shared via a class-selector field — see [VSET](08-vector.md). On a core without VSET, `0xF` decodes as RET.

## Encoding summary

| Family | Format | Fields | Immediate | Range |
|---|---|---|---|---|
| RRR | `OPCODE RD RS1 RS2 0000…` | 4 + 4 + 4 + 4 + 16 | none | register only |
| RI | `OPCODE RD RS1 IMM16` | 4 + 4 + 4 + 16 | 16-bit signed | ±32 K (addr) |
| BR | `OPCODE 0 RS1 0 OFFSET16` | 4 + 4 + 4 + 4 + 16 | 16-bit ×4 signed | ±128 KiB |
| JMP | `OPCODE 0…0 OFFSET16` | 4 + 12 + 16 | 16-bit ×4 signed | ±128 KiB |
| RET | `OPCODE 0 RS1 0 IMM16` | 4 + 4 + 4 + 4 + 16 | none (`PC ← rs1`) | register only |

## Alignment and PC-relative addressing

All instructions are naturally aligned to 4-byte boundaries. The program counter always points to a valid instruction word. Branch and jump targets are always 4-byte aligned — the low 2 bits of the computed address are undefined and ignored by the fetch unit.

The offset multiplication by 4 (for BR and JMP) ensures that the stored offset encodes word-counts, not byte-counts, doubling the effective branch range without widening the offset field.
