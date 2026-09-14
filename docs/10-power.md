# Power Management

BradISA V2 adds a hardware power-management subsystem: C-states for idle, DVFS for dynamic voltage/frequency scaling, per-unit clock-gating, and performance counters. All controlled via MSRs (see [MSR reference](07-msr-reference.md)).

## C-states

C-states define the processor's idle depth. Deeper states save more power but have higher wake-up latency.

| State | Name | Status | Wake latency | Power |
|---|---|---|---|---|
| C0 | Active | executing instructions | — | nominal |
| C1 | Wait-for-interrupt | core halted, clocks running | 1 cycle | reduced |
| C1E | WFI + Vmin | halted, voltage at minimum | 2 cycles | low |
| C2 | Sleep | clocks gated, registers retained | 8 cycles | very low |
| C2E | Sleep + power-gated | clocks gated, power-gated, state saved to SRAM | 32 cycles | near zero |
| C3 | Deep sleep | full power-down, state lost | 128 cycles (reboot) | zero |

### C0 — Active

Normal execution. All units powered and clocked. No power savings.

### C1 — Wait-for-Interrupt (WFI)

Core halts (`STATUS.HALTED = 1`) but all state is retained. Clocks continue running at full frequency. Wake-up on any enabled interrupt (`STATUS.IE = 1` and pending IRQ bit set). Latency: 1 cycle.

```asm
MSRWR r0, STATUS     ; set bit 2 (HALTED = 1)
```

WFI is the standard idle instruction. The core resumes at the next instruction when an interrupt arrives.

### C1E — WFI + Vmin

Same as C1, but the DVFS governor simultaneously reduces voltage to the minimum supported level (`VOLTAGE` MSR). This saves dynamic power without losing state. Wake-up latency: 2 cycles.

Entered via `PM_CTRL` with `CSTATE = 2`.

### C2 — Sleep

All clocks are gated (`CG_MASK` bits [7:0] forced to 0 by hardware). Register file and MSRs are retained. Wake-up requires an external interrupt — the core does not poll. Latency: 8 cycles.

Entered via `PM_CTRL` with `CSTATE = 3`.

### C2E — Sleep + Power-Gated

Same as C2, but the core's combinational logic is power-gated. Register file contents are saved to a retention SRAM (L1-size, ~4 KiB). On wake, the SRAM is restored and the core resumes. Latency: 32 cycles.

Entered via `PM_CTRL` with `CSTATE = 4`.

### C3 — Deep Sleep

Full power-down. All state is lost — including MSRs, register file, and pipeline state. The core must reboot from address `0x00` on wake. Latency: 128 cycles (full reset sequence).

Entered via `PM_CTRL` with `CSTATE = 5`.

## Transition model

Power-state transitions are managed via `PM_CTRL` (MSR 8):

```
write PM_CTRL with CSTATE = target
→ hardware transitions (STATUS.HALTED set automatically)
→ PM_CTRL.ACK cleared when transition complete
→ poll PM_CTRL.ACK == 0 to confirm
```

The `FORCE` bit (PM_CTRL bit 3) overrides the residency timer — use it for urgent transitions.

### Residency timer

Each C-state has a minimum residency time — the core must stay in the state for a minimum number of cycles before transitioning to a shallower state. This prevents thrashing:

| State | Min residency |
|---|---|
| C1 | 10 cycles |
| C1E | 50 cycles |
| C2 | 200 cycles |
| C2E | 1,000 cycles |
| C3 | 10,000 cycles |

The hardware enforces residency. A transition request that violates residency is queued and executed when the timer expires.

## DVFS — Dynamic Voltage and Frequency Scaling

The DVFS governor adjusts voltage and frequency in tandem to maintain a safe operating point.

| MSR | Description |
|---|---|
| `VOLTAGE` (11) | current voltage (µV), read/write |
| `FREQ` (12) | current frequency (Hz), read/write |
| `PWR_CAP` (14) | power cap (µW), read/write |
| `TEMP` (15) | die temperature (m°C), read-only |
| `ENERGY` (13) | cumulative energy (µJ), read-only |

### DVFS flow

1. Read `TEMP` — if above thermal threshold, reduce frequency
2. Read `ENERGY` — if energy budget exceeded, reduce voltage/frequency
3. Write `FREQ` — request new frequency; the governor adjusts voltage automatically
4. Write `PWR_CAP` — set the power envelope; the governor will not exceed it

Typical operating points:

| Core | Voltage | Frequency | Power |
|---|---|---|---|
| Falcon active | 900 mV | 3.6 GHz | ~5 W |
| Falcon idle (C1) | 600 mV | 1.2 GHz | ~0.5 W |
| Kestrel active | 900 mV | 3.5 GHz | ~8 W |
| Kestrel idle (C1) | 600 mV | 1.0 GHz | ~0.8 W |
| Phoenix active | 1000 mV | 5.2 GHz | ~15 W |
| Phoenix idle (C1) | 700 mV | 1.5 GHz | ~1.5 W |

### Thermal throttling

If `TEMP` exceeds the platform threshold (typically 105,000 m°C = 105 °C), the hardware automatically reduces `FREQ` and `VOLTAGE` to prevent damage. Throttling is indicated by setting `STATUS.HALTED` temporarily until the temperature drops below the hysteresis band (typically 95 °C).

## Clock-gating

`CG_MASK` (MSR 21) controls per-stage clock gating. Each bit corresponds to a pipeline stage:

| Bit | Stage | Effect of clearing |
|---|---|---|
| 0 | fetch | no new instructions fetched |
| 1 | decode | decode unit halted |
| 2 | rename | register rename halted |
| 3 | dispatch | dispatch logic halted |
| 4 | issue | issue queue halted |
| 5 | execute | execution units halted |
| 6 | memory | load/store unit halted |
| 7 | writeback | writeback stage halted |

Default: `0xFF` (all clocks running). The OS can selectively gate stages to save power during light workloads.

### Software clock-gating strategy

```
; light workload — gate back-end
MSRWR r0, CG_MASK    ; write 0x0F (bits 0-3 on, bits 4-7 off)
; front-end stays warm (fetch, decode, rename, dispatch)
; back-end is power-gated (issue, execute, memory, writeback)
```

This is effective for interrupt-heavy workloads where the core spends most of its time in the interrupt handler and the back-end is idle between interrupts.

## Performance counters

Two 32-bit performance counters (`PERF_CNT0`, `PERF_CNT1`) and a control register (`PERF_CNT_CTRL`) provide hardware-level profiling.

### Counter control

`PERF_CNT_CTRL` (MSR 18) layout:

| Bits | Field | Description |
|---|---|---|
| [3:0] | CNT0 | event select for counter 0 |
| [7:4] | CNT1 | event select for counter 1 |
| [8] | EN0 | enable counter 0 |
| [9] | EN1 | enable counter 1 |

### Event codes

| Code | Event | Useful for |
|---|---|---|
| 0 | cycles | timing |
| 1 | instructions retired | throughput |
| 2 | branches | branch density |
| 3 | branch mispredictions | branch predictor effectiveness |
| 4 | load-use stalls | data hazard analysis |
| 5 | L1 cache misses | cache tuning |
| 6 | L2 cache accesses | L2 bandwidth |
| 7 | L3 cache accesses | L3 bandwidth |
| 8 | stall cycles | pipeline efficiency |
| 9 | IPC (instantaneous) | real-time IPC |

### Usage example

```asm
; count instructions and cycles
ADDI r1, r0, 1       ; event 1 = instructions retired
ADDI r1, r1, 0       ; event 0 = cycles (shifted to bits [7:4])
MSRWR r1, PERF_CNT_CTRL  ; select events
MSRSET r1, PERF_CNT_CTRL ; enable both counters (bits 8,9)

; ... code to profile ...

MSRRD r2, PERF_CNT0  ; r2 = cycle count
MSRRD r3, PERF_CNT1  ; r3 = instruction count
; IPC = r3 / r2 (compute in software)
```

## Cache partitioning

`L2_PART` (MSR 19) and `L3_PART` (MSR 20) divide cache between CPU and GPU partitions:

| Bits | Partition |
|---|---|
| [7:0] | partition 0 size (cache lines × 4) |
| [15:8] | partition 1 size (cache lines × 4) |

Partition 0 is typically assigned to the BradVector GPU engine; partition 1 to the BradISA CPU. The sum of both partitions must not exceed the total cache capacity.

## Power management in the BradFusion fabric

The BradFusion fabric coordinates power across CPU cores, GPU, NPU, and memory controller. Each component has its own C-state and DVFS domain. The fabric governor orchestrates transitions to prevent one component from waking another prematurely.

Typical flow:

1. GPU completes a compute kernel → transitions to C2E
2. CPU completes a task → transitions to C2E
3. Memory controller enters self-refresh
4. Fabric governor waits for external interrupt
5. Interrupt arrives → fabric wakes CPU → CPU wakes GPU → work resumes

This coordinated approach is why BradOS Compute workstations achieve high efficiency at rack scale — the fabric keeps everything asleep until it's needed.
