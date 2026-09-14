# Data Model

How data is represented, addressed, and accessed in BradISA.

## Data types

BradISA V1 is an integer-only architecture. All data types are carried as 32-bit unsigned integers; interpretation is performed by instructions.

| Type | Width | Notes |
|---|---|---|
| Byte | 8 bits | loaded/stored via `LB`/`SB` (V2 byte-access) |
| Half-word | 16 bits | loaded/stored via `LH`/`SH` (V2 byte-access) |
| Word | 32 bits | native; all GPRs are 32 bits |
| Double-word | 64 bits | assembled from two consecutive words via `LDW` |
| VSET vector | 256 bits | single vector register; see [VSET](08-vector.md) |

V1 does not have dedicated byte or half-word memory instructions. All loads and stores are word-width (`LDW` / `STW`). The V2 compressed extension adds byte-level load/store instructions for stack-adjacent character operations (see [Compressed](09-compressed.md)).

## Endianness

BradISA is **little-endian**. A 32-bit word stored to address `A` occupies:

| Byte address | Stored bits |
|---|---|
| A+0 | `[7:0]` |
| A+1 | `[15:8]` |
| A+2 | `[23:16]` |
| A+3 | `[31:24]` |

Multi-word values follow the same rule: the least-significant word lives at the lower address.

## Address space

| Property | Value |
|---|---|
| Address width | 32 bits |
| Byte addressable | yes |
| Total address space | 4 GiB (2^32 bytes) |
| Alignment | word-aligned for LDW/STW (4 bytes) |
| Alignment fault | 0x08 page-fault / 0x0C unaligned |

The address space is flat and physically mapped. There is no MMU and no virtual addressing in V1. The `PAGE_BASE` MSR (0x04) exists as an implementation-defined base register for systems that layer physical partitioning or protection at the fabric level (see [MSR reference](07-msr-reference.md)).

## Page size

The minimum allocation unit enforced by the BradFusion fabric is:

| Property | Value |
|---|---|
| Page size | 4,096 bytes (4 KiB) |
| Alignment | addresses must be page-aligned for DMA transfers |
| Page fault | `STATUS[0x08]` set when a non-page-aligned access crosses a page boundary |

The ISA itself does not impose page granularity on register-to-register operations. The page constraint is a fabric-level rule: bulk transfers through `BRADFUSION_FABRIC` must start at a 4,096-byte boundary.

## Load / store rules

All register transfers are 32-bit words. There is no partial-register write — every `STW` writes 32 bits, every `LDW` reads 32 bits.

| Instruction | Operation | Alignment |
|---|---|---|
| `LDW rd, [rs1 + imm]` | rd ← mem32[rs1 + sext(imm)] | 4-byte aligned |
| `STW rs2, [rs1 + imm]` | mem32[rs1 + sext(imm)] ← rs2 | 4-byte aligned |

Address computation: base register + sign-extended 16-bit immediate. Result must be 4-byte aligned; otherwise `STATUS[0x08]` is set and a page-fault trap is taken.

## V2 byte and half-word access

V2 introduces byte-level instructions to support the compressed extension's stack discipline:

| Instruction | Width | Operation |
|---|---|---|
| `LDB rd, [rs1 + imm]` | 8-bit load | zero-extends to 32 bits |
| `LDH rd, [rs1 + imm]` | 16-bit load | zero-extends to 32 bits |
| `STB rs2, [rs1 + imm]` | 8-bit store | stores bits [7:0] only |
| `STH rs2, [rs1 + imm]` | 16-bit store | stores bits [15:0] only |

These are added under the compressed extension class in `bradisa_v2_ext.tex` section 3.1.

## Signed arithmetic

All integer ALU operations (`ADD`, `ADDI`, `SUB`, etc.) treat operands as **unsigned** by default. The 32-bit result is identical for signed and unsigned addition/subtraction — the same binary representation is correct for both, provided overflow is handled at the application level.

Sign-extension of immediates is performed by `ADDI`, `LDW`, `STW`, and branch instructions: the 16-bit immediate is sign-extended to 32 bits before the operation.

## Barrel shifting

Shift amounts are taken from the low 5 bits of the source register (`rs2[4:0]` for `SHL`/`SHR`), giving a range of 0–31 positions. This is a barrel shifter — the shift executes in a single cycle on all known core implementations.

## Multiplication

`MUL` produces the low 32 bits of the 32×32 multiply. High-32 is discarded. For full 64-bit multiply, the application must issue two `MUL` operations and combine results using `ADDI` and `SHL`.

Falcon-Lite (V2) does not implement `MUL` in hardware: it returns `0`. The assembler shall emit a software multiply sequence or trap — see [toolchain](11-toolchain.md).

## Null handling

Address `0x00000000` is valid and used as the reset entry point. It is not treated specially by the data path — the null pointer is `r0` (hardwired zero), not address zero.

## Floating-point

BradISA V1 is integer-only. VSET introduces 32-bit single-precision and 64-bit double-precision floating-point via the vector register file — there are no scalar FP instructions. The CPU-side scalar pipeline never encounters an FP opcode. See [VSET](08-vector.md).

## Byte ordering for multi-word vectors

VSET vector registers are stored in little-endian order within the register file. Vector element `v[i]` at byte offset `i*SEW_bytes` maps to the least-significant bits of the 256-bit vector. The BVRT runtime and `bradc` both enforce this layout.
