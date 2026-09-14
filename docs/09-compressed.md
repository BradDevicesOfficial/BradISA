# Compressed Instructions — 16-bit Encoding

The compressed extension halves instruction width from 32 bits to 16 bits for common operations, reducing code size by 30–40%. It is enabled by `STATUS.C` (MSR 0, bit 4) and targets Falcon and Kestrel cores.

## Overview

| Property | Value |
|---|---|
| Width | 16 bits |
| Alignment | 2-byte (PC increments by 2 when enabled) |
| Enable bit | `STATUS.C` (MSR 0, bit 4) |
| Code-size reduction | 30–40% |
| Backwards-compatible | yes — old 32-bit code still runs when C=1 |

When `STATUS.C = 0`, the core fetches 32-bit instructions and increments PC by 4 (standard BradISA V1). When `STATUS.C = 1`, the core fetches 16-bit instructions and increments PC by 2. The decoder determines the instruction width from the top bits of the fetched halfword.

## Enabling compressed mode

The OS sets `STATUS.C` before entering user mode:

```asm
MSRSET r0, STATUS     ; set bit 4 → C = 1
```

If the OS does not set C, the core runs in standard 32-bit mode and all compressed instructions are treated as 32-bit — they simply don't appear in the instruction stream.

## Decoder rule

The top 4 bits of a 16-bit halfword determine the compressed format:

| Bits [15:12] | Format | Description |
|---|---|---|
| `0000`–`0111` | CRRR | compact register-register-register |
| `1000`–`1011` | CRI | compact register-immediate |
| `1100`–`1101` | CBR | compact branch |
| `1110` | CMV | compact move / load-store |
| `1111` | CNOP / CRET | compact NOP or RET |

If the top bits do not match any compressed format, the decoder treats the halfword as the low 16 bits of a 32-bit instruction (fetching the next halfword for the high bits). This provides seamless interworking.

## Instruction formats

### CRRR — compact register-register-register

```
 15  12 11   9 8   6 5   3 2   0
+------+-------+-------+-------+-------+
| FMT  |   RD  |  RS1  |  RS2  | FUNCT |
| [4]  |  [3]  |  [3]  |  [3]  |  [3]  |
+------+-------+-------+-------+-------+
```

Fields: `FMT` 4 · `RD` 3 · `RS1` 3 · `RS2` 3 · `FUNCT` 3

3-bit register fields index a subset of the register file:

| 3-bit index | Full register |
|---|---|
| 000 | r0 |
| 001 | r1 |
| 010 | r2 |
| 011 | r3 |
| 100 | r4 |
| 101 | r5 |
| 110 | r6 |
| 111 | r7 |

Only `r0`–`r7` are accessible via CRRR. To use `r8`–`r15`, the compiler must emit 32-bit instructions or use a `CMV` to move values into the low register set.

### CRI — compact register-immediate

```
 15  12 11   9 8   6 5         0
+------+-------+-------+-----------+
| FMT  |   RD  |  RS1  |   IMM6    |
| [4]  |  [3]  |  [3]  |   [6]     |
+------+-------+-------+-----------+
```

6-bit signed immediate, range −32 to +31.

### CBR — compact branch

```
 15  12 11   9 8         3 2   0
+------+-------+-----------+-------+
| FMT  |  RS1  |  OFFSET8  | FUNCT |
| [4]  |  [3]  |   [6]     |  [3]  |
+------+-------+-----------+-------+
```

8-bit signed offset, multiplied by 2 (PC-relative, byte offset). Range: ±256 bytes.

### CMV — compact move / load-store

```
 15  12 11   9 8   6 5   3 2   0
+------+-------+-------+-------+-------+
| FMT  |   RD  |  RS1  |  RS2  | FUNCT |
| [4]  |  [3]  |  [3]  |  [3]  |  [3]  |
+------+-------+-------+-------+-------+
```

Same layout as CRRR, but used for moves and small load/store operations.

### CNOP / CRET

```
 15  12 11                          0
+------+------------------------------+
| 1111 |         0000000000           |
+------+------------------------------+
```

`0xF000` = CNOP (compact NOP, 2-byte aligned). `0xF001` = CRET (compact RET, returns to LR).

## Instruction set — complete list

### Arithmetic (CRRR)

| Opcode | Mnemonic | Operation | Cycles |
|---|---|---|---|
| `0x00` | C.ADD | rd = rs1 + rs2 | 1 |
| `0x01` | C.SUB | rd = rs1 − rs2 | 1 |
| `0x02` | C.AND | rd = rs1 & rs2 | 1 |
| `0x03` | C.OR | rd = rs1 \| rs2 | 1 |
| `0x04` | C.XOR | rd = rs1 ^ rs2 | 1 |
| `0x05` | C.MUL | rd = low32(rs1 × rs2) | 3 |
| `0x06` | C.SHL | rd = rs1 << rs2[4:0] | 1 |
| `0x07` | C.SHR | rd = rs1 >> rs2[4:0] (logical) | 1 |

### Immediate (CRI)

| Opcode | Mnemonic | Operation | Cycles |
|---|---|---|---|
| `0x08` | C.ADDI | rd = rs1 + sext(imm6) | 1 |
| `0x09` | C.LW | rd = mem32[rs1 + sext(imm6)×4] | 3+ |
| `0x0A` | C.SW | mem32[rs1 + sext(imm6)×4] = rs2 | 1 |
| `0x0B` | C.ANDI | rd = rs1 & sext(imm6) | 1 |

### Branch (CBR)

| Opcode | Mnemonic | Operation | Cycles |
|---|---|---|---|
| `0x0C` | C.BZ | if rs1 == 0: PC += sext(offset8)×2 | 1/3 |
| `0x0D` | C.BNZ | if rs1 != 0: PC += sext(offset8)×2 | 1/3 |

### Move / Load-store (CMV)

| Opcode | Mnemonic | Operation | Cycles |
|---|---|---|---|
| `0x0E` | C.MV | rd = rs1 (register move) | 1 |
| `0x0E` | C.LB | rd = mem8[rs1 + offset] (byte load, zero-extended) | 3+ |
| `0x0E` | C.SB | mem8[rs1 + offset] = rs2[7:0] (byte store) | 1 |
| `0x0E` | C.LH | rd = mem16[rs1 + offset] (half-word load, zero-extended) | 3+ |
| `0x0E` | C.SH | mem16[rs1 + offset] = rs2[15:0] (half-word store) | 1 |

The `FUNCT` field in CMV distinguishes byte/half-word/word operations.

### Control (CNOP / CRET)

| Opcode | Mnemonic | Operation | Cycles |
|---|---|---|---|
| `0x0F` | C.NOP | no operation | 1 |
| `0x0F` | C.RET | PC ← LR | 3 |

## Register constraints

Compressed instructions only address `r0`–`r7` (the low register set). This is intentional — 3-bit fields select among 8 registers. For operations on `r8`–`r15`, the compiler must either:

1. Use a 32-bit instruction, or
2. Use a `C.MV` to move the value into a low register, operate on it there, then move back

The ABI already allocates `r1`–`r7` (a0–t2) as caller-saved — these are the most frequently used registers and benefit most from compressed encodings.

## PC model

When `STATUS.C = 1`:

- PC increments by 2 after each compressed instruction fetch
- Branch targets are 2-byte aligned (low bit of target is 0)
- The vector table entries (`0x00`–`0x2C`) are still 4-byte aligned — the handler code is always 32-bit
- Calling a 32-bit function from compressed code: the return address saved in LR is the address of the next 32-bit instruction (PC + 4), which may not be 2-byte aligned — this is fine, the decoder handles it

## Interworking with 32-bit code

When `STATUS.C = 1`, the decoder inspects the top 4 bits of each 16-bit fetch. If they do not match a compressed format, the decoder fetches the next 16 bits and combines them into a 32-bit instruction. This means:

- 16-bit and 32-bit instructions can be freely interleaved
- No mode switch is needed to call a 32-bit function from 16-bit code
- The return address is always correct (points to the next instruction, regardless of width)

## Code size impact

| Benchmark | 32-bit only | With compressed | Reduction |
|---|---|---|---|
| saxpy (10 insns) | 40 bytes | 28 bytes | 30% |
| matrix multiply | 256 bytes | 160 bytes | 37% |
| fibonacci (recursive) | 128 bytes | 80 bytes | 37% |
| typical OS kernel | 12.8 KiB | 8.2 KiB | 36% |

Average reduction across the BradISA test suite: **34%**.

## Relationship to other extensions

| Extension | Interaction |
|---|---|
| VSET | vector instructions are always 32-bit; compressed only affects scalar code |
| Power management | C-states and DVFS are independent of compressed mode |
| SVEXT (Gen2) | compressed is fully supported on Gen2 with variable VLEN |
