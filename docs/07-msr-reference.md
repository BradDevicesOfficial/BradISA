# Model-Specific Register Reference

BradISA uses Model-Specific Registers (MSRs) for system control, performance monitoring, and power management. MSRs are accessed via dedicated instructions:

| Instruction | Operation |
|---|---|
| `MSRWR rd, MSR#` | write MSR `rd` ← value (supervisor only) |
| `MSRRD rd, MSR#` | read MSR value → `rd` |
| `MSRSET rd, MSR#` | set bits (atomic OR) |
| `MSRCLR rd, MSR#` | clear bits (atomic AND-NOT) |

All MSR access from user mode (`STATUS.SVC = 0`) raises cause `0x10` (privileged instruction).

## MSR map

| # | Name | V1 | V2 | Access | Description |
|---|---|---|---|---|---|
| 0 | STATUS | ✓ | ✓ | R/W | system status and control |
| 1 | CAUSE | ✓ | ✓ | R/W | exception cause code |
| 2 | EPC | ✓ | ✓ | R | exception program counter |
| 3 | EAR | ✓ | ✓ | R | exception address (faulting addr) |
| 4 | PAGE_BASE | ✓ | ✓ | R/W | page base address |
| 5 | TICK | ✓ | ✓ | R/W | free-running timer |
| 6 | CORE_ID | ✓ | ✓ | R | core identification |
| 7 | CLUSTER_ID | ✓ | ✓ | R | cluster identification |
| 8 | PM_CTRL | — | ✓ | R/W | power management control |
| 9 | PSTATE | — | ✓ | R | power state |
| 10 | PSTATES | — | ✓ | R | supported power states bitmap |
| 11 | VOLTAGE | — | ✓ | R/W | current voltage |
| 12 | FREQ | — | ✓ | R/W | current frequency (Hz) |
| 13 | ENERGY | — | ✓ | R | energy consumed (Joules) |
| 14 | PWR_CAP | — | ✓ | R/W | power cap |
| 15 | TEMP | — | ✓ | R | die temperature |
| 16 | PERF_CNT0 | — | ✓ | R | performance counter 0 |
| 17 | PERF_CNT1 | — | ✓ | R | performance counter 1 |
| 18 | PERF_CNT_CTRL | — | ✓ | R/W | performance counter control |
| 19 | L2_PART | — | ✓ | R/W | L2 cache partitioning |
| 20 | L3_PART | — | ✓ | R/W | L3 cache partitioning |
| 21 | CG_MASK | — | ✓ | R/W | clock-gating mask |
| 22 | DBG_CTRL | — | ✓ | R/W | debug control |
| 23 | VSTATUS | — | ✓ | R/W | vector status |

---

## MSR 0 — STATUS

System status and control register. Single-bit flags that govern execution mode and interrupt state.

| Bit | V1 | V2 | Name | R/W | Description |
|---|---|---|---|---|---|
| 0 | ✓ | ✓ | IE | R/W | global interrupt enable (1 = enabled) |
| 1 | ✓ | ✓ | SVC | R/W | supervisor mode (1 = supervisor) |
| 2 | ✓ | ✓ | HALTED | R/W | core halted (WFE / debug halt) |
| 3 | ✓ | ✓ | WFE | R/W | wait-for-event active |
| 4 | — | ✓ | C | R/W | compressed instruction mode (1 = PC +2) |
| 5 | — | ✓ | EE | R/W | extended exceptions (V2 feature enable) |
| 6 | — | ✓ | DBG | R/W | debug mode enable |
| 8 | ✓ | ✓ | EXT_IRQ0 | R/W | external interrupt 0 pending/mask |
| 9 | ✓ | ✓ | EXT_IRQ1 | R/W | external interrupt 1 pending/mask |
| 10 | ✓ | ✓ | EXT_IRQ2 | R/W | external interrupt 2 pending/mask |
| 11 | ✓ | ✓ | EXT_IRQ3 | R/W | external interrupt 3 pending/mask |

### Usage examples

```asm
; enable interrupts
MSRSET r0, STATUS     ; set bit 0 (IE = 1)

; disable interrupts
MSRCLR r0, STATUS     ; clear bit 0 (IE = 0)

; enter supervisor mode (from handler entry)
MSRSET r0, STATUS     ; set bit 1 (SVC = 1)

; enable compressed mode (V2)
MSRSET r0, STATUS     ; set bit 4 (C = 1)

; halt core (wait for interrupt)
MSRWR  r0, STATUS     ; set bit 2 (HALTED = 1)
```

---

## MSR 1 — CAUSE

Contains the 4-bit cause code of the most recent exception. Read to determine the trap type in an exception handler. Write in supervisor mode to update (useful for software-triggered traps).

| Value | Cause | Vector addr |
|---|---|---|
| 0x00 | reset | 0x00 |
| 0x01 | undefined instruction | 0x04 |
| 0x02 | page fault | 0x08 |
| 0x03 | unaligned access | 0x0C |
| 0x04 | privileged instruction | 0x10 |
| 0x05 | syscall / ecall | 0x14 |
| 0x06 | timer | 0x18 |
| 0x07 | external IRQ 0 | 0x20 |
| 0x08 | external IRQ 1 | 0x24 |
| 0x09 | external IRQ 2 | 0x28 |
| 0x0A | external IRQ 3 | 0x2C |

---

## MSR 2 — EPC

Exception Program Counter. Holds the address of the instruction that caused the most recent trap. Set automatically on trap entry; read-only thereafter. On V2, the `RFE` instruction reads `EPC` and resumes execution there.

---

## MSR 3 — EAR

Exception Address Register. Holds the memory address that caused a page-fault (`CAUSE = 0x02`) or unaligned-access (`CAUSE = 0x03`) exception. Not set for other exception types.

---

## MSR 4 — PAGE_BASE

Page base address register. An implementation-defined base for physical memory partitioning or protection at the BradFusion fabric level. The ISA does not mandate its use — behaviour is defined by the fabric implementation.

---

## MSR 5 — TICK

Free-running 32-bit timer counter. Increments every cycle (or at a platform-defined rate). When it overflows from `0xFFFFFFFF` to `0x00000000`, cause `0x06` (timer) is raised if `STATUS.IE = 1`.

Used for periodic task scheduling and elapsed-time measurement.

```asm
MSRRD r1, TICK        ; r1 = current tick count
```

---

## MSR 6 — CORE_ID

Read-only. Returns the physical core number within a cluster. Phoenix octa-core: values 0–7. Falcon/Kestrel: always 0.

---

## MSR 7 — CLUSTER_ID

Read-only. Returns the cluster number. Single-cluster systems return 0.

---

## MSR 8 — PM_CTRL (V2)

Power management control. Used to transition between C-states and to request DVFS changes.

| Bits | Name | Description |
|---|---|---|
| [2:0] | CSTATE | requested C-state (see [Power](10-power.md)) |
| [3] | FORCE | force transition (ignore residency timer) |
| [4] | ACK | acknowledge pending transition |

Writing to `PM_CTRL` initiates a power-state transition. The hardware completes the transition and clears `ACK` when done. Polling `ACK == 0` confirms the transition is complete.

---

## MSR 9 — PSTATE (V2)

Read-only. Returns the current operating power state (C-state):

| Value | State |
|---|---|
| 0 | C0 — active |
| 1 | C1 — wait-for-interrupt |
| 2 | C1E — WFI + Vmin |
| 3 | C2 — sleep |
| 4 | C2E — sleep + power-gated |
| 5 | C3 — deep sleep |

---

## MSR 10 — PSTATES (V2)

Read-only bitmap of supported power states. Bit `n` set means C-state `n` is available. Typical: `0x3F` (all C-states supported).

---

## MSR 11 — VOLTAGE (V2)

Read/write. Current core voltage in microvolts (µV). Writing requests a DVFS change; the hardware transitions and stabilises. Typical values:

| State | Voltage |
|---|---|
| active | 900,000 µV (0.9 V) |
| idle | 600,000 µV (0.6 V) |
| sleep | 400,000 µV (0.4 V) |

---

## MSR 12 — FREQ (V2)

Read/write. Current core frequency in Hz. Writing requests a DVFS change. Typical values:

| State | Frequency |
|---|---|
| Falcon | 2.4–3.6 GHz |
| Kestrel | 3.5 GHz |
| Phoenix | 3.0–5.2 GHz |

---

## MSR 13 — ENERGY (V2)

Read-only. Cumulative energy consumed by the core since power-on, in microjoules (µJ).

---

## MSR 14 — PWR_CAP (V2)

Read/write. Maximum power envelope in microwatts (µW). The DVFS governor will not exceed this cap when scaling. If the requested frequency would exceed the cap, the governor reduces voltage first, then frequency.

---

## MSR 15 — TEMP (V2)

Read-only. Die temperature in millidegrees Celsius (m°C). 45,000 = 45 °C. Thermal throttling triggers at a platform-defined threshold (typically 105,000 m°C = 105 °C).

---

## MSR 16–17 — PERF_CNT0 / PERF_CNT1 (V2)

Read-only. 32-bit performance counters. Count events selected by `PERF_CNT_CTRL` (MSR 18). Counter 0 and counter 1 are independent and can count different events simultaneously.

---

## MSR 18 — PERF_CNT_CTRL (V2)

Performance counter control. Selects which events are counted by `PERF_CNT0` and `PERF_CNT1`.

| Bits | Counter | Description |
|---|---|---|
| [3:0] | CNT0 | event select for counter 0 |
| [7:4] | CNT1 | event select for counter 1 |
| [8] | EN0 | enable counter 0 |
| [9] | EN1 | enable counter 1 |

Event codes:

| Code | Event |
|---|---|
| 0 | cycles |
| 1 | instructions retired |
| 2 | branches |
| 3 | branch mispredictions |
| 4 | load-use stalls |
| 5 | L1 cache misses |
| 6 | L2 cache accesses |
| 7 | L3 cache accesses |
| 8 | stall cycles |
| 9 | IPC (instantaneous) |

---

## MSR 19 — L2_PART (V2)

L2 cache partitioning. Divides the L2 cache between two virtual partitions.

| Bits | Description |
|---|---|
| [7:0] | partition 0 size (cache lines × 4) |
| [15:8] | partition 1 size (cache lines × 4) |

Partition 0 is typically assigned to the GPU; partition 1 to the CPU. The sum must not exceed total L2 capacity.

---

## MSR 20 — L3_PART (V2)

L3 cache partitioning. Same layout as `L2_PART`. Divides the L3 SRAM (BradRAM) between CPU and GPU.

---

## MSR 21 — CG_MASK (V2)

Clock-gating mask. Each bit disables clock-gating for a pipeline stage. Clearing a bit enables gating for that stage (power savings). Setting a bit keeps the clock running (lower latency).

| Bit | Stage | Effect of clearing |
|---|---|---|
| 0 | fetch | gate fetch unit (no new insns fetched) |
| 1 | decode | gate decode unit |
| 2 | rename | gate register rename |
| 3 | dispatch | gate dispatch logic |
| 4 | issue | gate issue queue |
| 5 | execute | gate execution units |
| 6 | memory | gate load/store unit |
| 7 | writeback | gate writeback stage |

Default `0xFF` — all clocks running. Write `0x0F` to gate the back-end (issue/execute/memory/writeback) while the front-end stays warm.

---

## MSR 22 — DBG_CTRL (V2)

Debug control register.

| Bit | Description |
|---|---|
| 0 | breakpoint enable |
| 1 | single-step enable |
| 2 | trace enable |
| [7:4] | trace mode select |

Used by the reference emulator for interactive debugging. Write `0x03` to enable breakpoints and single-stepping; the core halts on the next breakpoint hit or after each instruction.

---

## MSR 23 — VSTATUS (V2)

Vector extension status.

| Bit | Name | Description |
|---|---|---|
| 0 | VE | VSET extension enable (1 = enabled) |
| 1 | VILL | vector illegal instruction (sticky, read to clear) |
| 2 | VFP | V2 vector floating-point enable (1 = FP opcodes active) |
| [31:8] | SVL | (Gen2) supported vector length — read to probe hardware |

`VE` must be set before executing any VSET instruction. If `VE = 0` and a VSET opcode is encountered, cause `0x10` (privileged instruction) is raised — the OS must enable VSET for user processes.

`VILL` is set when a vector instruction with an unsupported SEW or illegal encoding is attempted. It is sticky — read it to clear.

`VFP` gates the VFP instruction class (class `001`). When `VFP = 0`, floating-point vector instructions raise `0x01` (undefined instruction).

`SVL` (bits [31:8]) is reserved in Gen1 — reads as 0. In Gen2 (SVEXT), it indicates the hardware-supported vector length (128/256/512 bits).

---

## MSR access encoding

The MSR number is carried in the `RD` field of the `MSRWR` / `MSRRD` instruction. The `RS1` field carries the data (for writes) or is ignored (for reads). For `MSRSET` / `MSRCLR`, `RS1` carries the bit mask.

```asm
MSRRD r1, STATUS     ; r1 = STATUS value
MSRWR r0, STATUS     ; STATUS ← r0 (supervisor only)
MSRSET r2, STATUS    ; STATUS |= r2 (set bits)
MSRCLR r3, STATUS    ; STATUS &= ~r3 (clear bits)
```
