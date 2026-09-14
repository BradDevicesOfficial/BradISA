# Exceptions and Interrupts

BradISA defines a fixed vector table, 8 cause codes, and a two-mode execution model. All exception handling is software-defined — the hardware performs no register save.

## Execution modes

| Mode | `STATUS.SVC` | Description |
|---|---|---|
| User | 0 | normal application code |
| Supervisor | 1 | OS / exception handler code |

The mode bit is set automatically when a trap is taken and cleared on `RFE`. User-mode code cannot modify `STATUS.SVC` directly — an attempt raises cause `0x10` (privileged instruction).

## Vector table

The vector table is an array of 11 four-byte words at fixed addresses:

| Address | Cause | Name | Trigger |
|---|---|---|---|
| `0x00` | `0x00` | reset | power-on / warm-reset |
| `0x04` | `0x01` | undefined instruction | opcode not in the set (or reserved encoding) |
| `0x08` | `0x02` | page fault | misaligned LDW/STW or DMA crossing a page boundary |
| `0x0C` | `0x03` | unaligned access | LDW/STW to a non-4-byte-aligned address |
| `0x10` | `0x04` | privileged instruction | user-mode write to MSR or `STATUS` |
| `0x14` | `0x05` | syscall / ecall | `CALL` instruction executed (software trap) |
| `0x18` | `0x06` | timer | `TICK` counter overflow (periodic interrupt) |
| `0x1C` | — | reserved | — |
| `0x20` | `0x07` | external IRQ 0 | BradFusion fabric interrupt line 0 |
| `0x24` | `0x08` | external IRQ 1 | BradFusion fabric interrupt line 1 |
| `0x28` | `0x09` | external IRQ 2 | BradFusion fabric interrupt line 2 |
| `0x2C` | `0x0A` | external IRQ 3 | BradFusion fabric interrupt line 3 |

Each entry contains the address of the handler for that cause. The processor fetches from the vector table address on trap entry.

## Trap flow

When a trap occurs:

1. **Save PC:** `EPC ← PC` (address of the faulting or trapping instruction)
2. **Disable interrupts:** `STATUS.IE ← 0`
3. **Enter supervisor mode:** `STATUS.SVC ← 1`
4. **Set cause:** `CAUSE ← cause_code`
5. **Fetch handler:** `PC ← vector_table[cause_code]`

The handler executes in supervisor mode with interrupts disabled. It must:

1. Save all registers it will modify (no hardware register save)
2. Read `CAUSE` to determine the trap type
3. Read `EAR` if the trap was a memory fault (the faulting address)
4. Handle the trap
5. Restore registers
6. Clear `STATUS.SVC` (return to user mode) — or leave set if the handler remains in supervisor mode
7. Resume execution at `EPC` (or at a different address, if the handler chooses)

## Return from exception

V1 defines a software return convention: the handler writes the return address to `r15` (PC) and clears `STATUS.IE` as appropriate:

```asm
exception_return:
  ; r14 holds saved EPC (set by handler entry code)
  ADDI r15, r14, 0      ; PC ← EPC (resume)
```

V2 adds an explicit `RFE` instruction (opcode `0xF`, funct `0x11`) that atomically restores `PC ← EPC`, `STATUS.IE ← 1`, and `STATUS.SVC ← 0` in a single instruction. See [Compressed](09-compressed.md).

## Exception handler entry (typical)

```asm
vector_reset:
  ; set up stack and supervisor mode
  ADDI r13, r0, 0x80000000   ; sp = top of SRAM
  ADDI r14, r0, 0            ; lr = 0 (no prior context)
  JMP  main                  ; jump to kernel main

vector_undef:
  ; save registers to kernel stack
  STW  r14, [r13, -4]        ; save return address
  ADDI r13, r13, -64         ; allocate 16 words
  STW  r1,  [r13, 0]
  STW  r2,  [r13, 4]
  ; ... save r3-r12 ...
  STW  r12, [r13, 44]
  LDW  r14, [r13, 60]        ; restore caller's lr (= saved EPC)
  ; handle: read CAUSE, dispatch
  ; restore registers
  LDW  r1,  [r13, 0]
  ; ...
  ADDI r13, r13, 64
  ADDI r15, r14, 0           ; resume at EPC
```

## External interrupts

External interrupts (`0x20`–`0x2C`) are routed through the BradFusion fabric. Each interrupt line is independently maskable via `STATUS` bits:

| Bit | Interrupt |
|---|---|
| 8 | EXT_IRQ0 |
| 9 | EXT_IRQ1 |
| 10 | EXT_IRQ2 |
| 11 | EXT_IRQ3 |

An interrupt is pending when its `STATUS` bit is set and `STATUS.IE = 1`. Only one interrupt is serviced at a time — priority is fixed: IRQ0 > IRQ1 > IRQ2 > IRQ3.

## Nested interrupts

V1 does not support nested interrupts by default. `STATUS.IE` is cleared on trap entry, preventing further interrupts. To enable nesting, the handler must explicitly set `STATUS.IE` after saving context — this is a software choice, not a hardware feature.

## Timer interrupt

The `TICK` MSR (0x05) is a free-running 32-bit counter. When it overflows from `0xFFFFFFFF` to `0x00000000`, cause `0x06` (timer) is raised if `STATUS.IE = 1`. The handler must clear the pending condition by writing to `TICK` or acknowledging the timer via the BradFusion fabric.

## Cause register

`CAUSE` (MSR 0x01) contains the cause code of the most recent exception. It is read-only in user mode. Writing to it in supervisor mode updates the value (useful for software-triggered exceptions).

```asm
MSRRD r1, CAUSE      ; r1 = cause code
```

## Exception program counter

`EPC` (MSR 0x02) holds the PC of the instruction that caused the trap. It is set automatically on trap entry and read-only thereafter (until `RFE` on V2).

## Exception address register

`EAR` (MSR 0x03) holds the faulting memory address for page-fault (`0x08`) and unaligned-access (`0x0C`) exceptions. It is not set for other exception types.

## Privileged instructions

In user mode (`STATUS.SVC = 0`), the following operations raise cause `0x10` (privileged instruction):

- Writing to any MSR
- Executing `RFE` (V2)
- Executing `WFE` / `SEV`
- Accessing vector registers (VSET) if `STATUS.VE = 0`

This is the hardware privilege boundary. The OS kernel runs in supervisor mode; applications run in user mode.

## Reset

On power-on or warm-reset, the processor:

1. Clears all GPRs to zero
2. Clears all MSRs to zero (STATUS = 0, interrupts disabled, user mode)
3. Fetches the first instruction from address `0x00`

The reset handler at `0x00` must set up the stack, enable interrupts, and jump to the kernel or application entry point.

## Nesting and preemption model

| Feature | V1 | V2 |
|---|---|---|
| Nested interrupts | software-managed | software-managed |
| Interrupt priority | fixed (IRQ0 highest) | fixed |
| Preemption | requires re-enabling IE in handler | same |
| Vector table | fixed at 0x00–0x2C | same |
