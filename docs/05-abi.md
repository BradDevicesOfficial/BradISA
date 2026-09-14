# Application Binary Interface

The BradISA calling convention defines register roles, stack layout, parameter passing, and return-value rules. All Brad ISA cores — Falcon, Kestrel, Phoenix — use this ABI.

## Register map

| Reg | Name | Role | Saved by |
|---|---|---|---|
| r0 | zero | hardwired zero | — |
| r1 | a0 | argument 0 / return value | caller |
| r2 | a1 | argument 1 | caller |
| r3 | a2 | argument 2 | caller |
| r4 | a3 | argument 3 | caller |
| r5 | t0 | scratch / temp 0 | caller |
| r6 | t1 | scratch / temp 1 | caller |
| r7 | t2 | scratch / temp 2 | caller |
| r8 | s0 | saved 0 | **callee** |
| r9 | s1 | saved 1 | **callee** |
| r10 | s2 | saved 2 | **callee** |
| r11 | s3 | saved 3 | **callee** |
| r12 | s4 | saved 4 | **callee** |
| r13 | sp | stack pointer | **callee** |
| r14 | lr | link register | caller |
| r15 | pc | program counter (read-only) | — |

### Register roles in detail

**r0 (zero):** Always reads as zero. Writes are discarded. Use as a constant zero source. `ADD r1, r0, r0` produces zero in r1 without touching memory.

**r1–r4 (a0–a3):** Arguments to a function, in order. `r1` also carries the return value. If the return value does not fit in 32 bits, the caller allocates space and passes a pointer in `r1`.

**r5–r7 (t0–t2):** Caller-saved temporaries. A function may use these freely; the caller must save them before a call if the values are needed afterward.

**r8–r12 (s0–s4):** Callee-saved. If a function uses any of these, it must restore them before returning. This is the contract that makes stack frames possible.

**r13 (sp):** Stack pointer. Points to the top of the current stack frame. Grows downward (toward lower addresses). Must be 4-byte aligned at all times. Preserved by the callee.

**r14 (lr):** Link register. Written by `CALL` with `PC + 4`. Read by `RET`. For nested calls, `lr` must be saved to the stack by the caller.

**r15 (pc):** Program counter. Read-only in software. Writes are discarded. The branch unit controls `PC` exclusively.

## Stack discipline

The stack grows **downward** (toward address 0). The stack pointer (`sp`, `r13`) points to the last allocated word — the most recently pushed value.

```
              high addresses
              ┌──────────────┐
              │  caller frame │
              ├──────────────┤ ← caller's sp
              │  saved lr    │  [sp + N-4]
              │  saved s0-s4 │  [sp + N-8 .. sp]
              │  local vars  │
              ├──────────────┤ ← callee's sp (on entry)
              │              │
              ▼  stack grows │
              low addresses
```

### Entry sequence (callee prologue)

A function that uses callee-saved registers or needs stack space:

```asm
func:
  STW  r14, [r13, -4]    ; save return address
  STW  r8,  [r13, -8]    ; save s0 (if used)
  ADDI r13, r13, -N      ; allocate frame (N = frame size, must be multiple of 4)
  ; ... function body ...
  ADDI r13, r13, N       ; deallocate frame
  LDW  r8,  [r13, -8]    ; restore s0
  LDW  r14, [r13, -4]    ; restore return address
  RET
```

The stack pointer must be restored to its entry value before `RET`.

### Leaf functions

A leaf function (one that makes no calls) need not save `lr`. It may use caller-saved temporaries freely and return with:

```asm
leaf_func:
  ; ... body using r1-r7 only ...
  RET
```

## Parameter passing

| Parameter | Register |
|---|---|
| arg 0 | r1 (a0) |
| arg 1 | r2 (a1) |
| arg 2 | r3 (a2) |
| arg 3 | r4 (a3) |
| arg 4+ | stack (pushed right-to-left before the call) |

Arguments that do not fit in 32 bits (e.g., 64-bit values) are passed as pairs: low word in `a0`, high word in `a1`, consuming two argument registers.

### Stack arguments

If a function has more than 4 arguments, the extra arguments are pushed onto the stack before the `CALL`:

```asm
; call func(a0, a1, a2, a3, a4, a5)
ADDI r1, r0, a0_val
ADDI r2, r0, a1_val
ADDI r3, r0, a2_val
ADDI r4, r0, a3_val
STW  r0, [r13, -4]      ; placeholder for alignment (if needed)
ADDI r13, r13, -8
STW  a5_val, [r13, 4]   ; push arg 5
STW  a4_val, [r13, 0]   ; push arg 4
CALL r14, func
ADDI r13, r13, 8        ; clean up stack arguments
```

Stack arguments are pushed in **right-to-left** order (last argument at the lowest address) so that argument 4 is at `[sp]` on entry to the callee.

## Return values

A 32-bit return value is placed in `r1` (a0) before `RET`. A 64-bit return value uses `r1` (low) and `r2` (high).

If the return value does not fit in registers (structs, large arrays), the caller allocates space and passes a pointer in `r1`.

## Calling another function

When a function calls another, it must:

1. Save `lr` if it has not already (nested call)
2. Save any caller-saved temporaries whose values are needed after the call
3. Push stack arguments (if any) in right-to-left order
4. Issue `CALL r14, target`
5. Clean up stack arguments after the call returns

```asm
caller:
  STW  r14, [r13, -4]    ; save lr (nested call)
  STW  r5,  [r13, -8]    ; save t0 (needed after call)
  ADDI r13, r13, -12     ; allocate frame
  ADDI r1, r0, 42        ; arg 0
  CALL r14, callee
  ; r1 now holds callee's return value
  LDW  r5,  [r13, 4]     ; restore t0
  ADDI r13, r13, 12      ; deallocate frame
  LDW  r14, [r13, -4]    ; restore lr
  RET
```

## Tail calls

A tail call is a `JMP` (not `CALL`) to another function when the current function's stack frame is no longer needed:

```asm
tail_call:
  ADDI r13, r13, N       ; deallocate own frame
  JMP  other_func        ; reuse caller's frame
```

Tail calls avoid stack growth and `lr` save/restore overhead.

## Vector ABI (VSET)

When the VSET extension is present, 32 vector registers (256-bit each) follow a separate ABI:

| Reg | Name | Role | Saved by |
|---|---|---|---|
| v0 | vm | mask / predicate | caller |
| v1 | va0 | argument 0 / return | caller |
| v2 | va1 | argument 1 | caller |
| v3 | va2 | argument 2 | caller |
| v4 | va3 | argument 3 | caller |
| v5 | vt0 | scratch 0 | caller |
| v6 | vt1 | scratch 1 | caller |
| v7 | vt2 | scratch 2 | caller |
| v8–v15 | vs0–vs7 | saved 0–7 | **callee** |
| v16–v31 | vt3–vt18 | scratch 3–18 | caller |

### Vector parameter passing

| Parameter | Register |
|---|---|
| vector arg 0 | v1 (va0) |
| vector arg 1 | v2 (va1) |
| vector arg 2 | v3 (va2) |
| vector arg 3 | v3 (va3) |
| vector arg 4+ | stack (256-bit aligned) |

### Vector return value

A single vector return value is placed in `v1` (va0). For multiple return values, `v1`–`v4` are used.

### Vector callee-saved registers

`v8`–`v15` (vs0–vs7) are callee-saved. A function that uses any of these must save and restore them. Saving a 256-bit vector register requires 32 bytes of stack space (8 words).

```asm
; save v8
STW  v8,  [r13, -32]    ; 32 bytes = 8 words
STW  v9,  [r13, -24]
ADDI r13, r13, -32
; ... use v8, v9 ...
ADDI r13, r13, 32
LDW  v9,  [r13, -24]
LDW  v8,  [r13, -32]
```

See [VSET](08-vector.md) for the full vector instruction reference.

## Exception conventions

When a trap is taken (see [Exceptions](06-exceptions.md)):

1. `EPC` is set to the address of the instruction that caused the trap
2. `STATUS.IE` is cleared (interrupts disabled)
3. `STATUS.SVC` is set (supervisor mode)
4. Control transfers to the vector table entry

The exception handler must save all registers it uses. There is no hardware register save — the handler is fully responsible for context preservation.

On `RFE`, the handler writes `EPC` to `r15` (or uses a dedicated `RFE` instruction on V2) to resume.

## Alignment rules

| Item | Required alignment |
|---|---|
| Stack pointer (sp) | 4-byte aligned |
| LDW / STW address | 4-byte aligned |
| Stack arguments | 4-byte aligned (pad if needed) |
| Vector registers on stack | 32-byte aligned (8 words) |
| DMA transfers | 4,096-byte aligned (page boundary) |

## System call convention

System calls (syscall number in `r0`, arguments in `r1`–`r3`) are used by `bradlib` for OS services. The syscall number table is defined in `bradlib.h`:

| r0 | Service |
|---|---|
| 1 | write(fd, buf, len) |
| 2 | read(fd, buf, len) |
| 3 | open(path, flags) |
| 4 | close(fd) |
| 5 | brk(addr) — set heap end |
| 93 | exit(code) |

The kernel preserves all registers except the return value in `r1`.
