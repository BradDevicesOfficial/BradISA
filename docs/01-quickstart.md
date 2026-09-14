# Quick-start

Build the BradISA toolchain and run your first program in under 10 minutes.

## Prerequisites

- Linux (x86-64 or AArch64), macOS, or WSL
- `gcc` or `clang`, `make` (or `cmake` ≥ 3.22), `git`
- Python 3.10+ (for `braddev` and the test harness)

Everything below assumes you are working from the BradISA repository root.

## 1 — Build bradc (the assembler)

```
cd src/bradc
make -j$(nproc)
sudo make install        # installs to /usr/local/bin
```

`bradisa` is the standalone assembler binary. You can also use the wrapper `bradc` which drives `bradisa` → `.bvbc` (BradVector Bytecode) in one step.

Verify:

```
bradc --version
# bradc 1.0.0 — Brad Vector Assembler
```

## 2 — Build the BVRT runtime

```
cd src/runtime
make -j$(nproc)
sudo make install
```

This installs the `bradvm` interpreter and `libbrad.so` (the host-side runtime library).

## 3 — Build bradgdb (optional, for interactive debugging)

```
cd src/bradgdb
make -j$(nproc)
sudo make install
```

## 4 — Your first program

Create a file called `hello.basm`:

```asm
; hello.basm — BradISA hello world
.section .text
  ; syscall 4 = write(fd=1, buf, len)
  ADDI  r1, r0, 1       ; fd = stdout
  ADDI  r2, r0, msg      ; buf = &msg
  ADDI  r3, r0, 14       ; len = 13 (including null)
  ADDI  r15, r0, 0       ; return address (we exit)
  ADDI  r0, r0, 0        ; NOP
  CALL  r14, 0           ; lr = pc+4, will be used by syscall

  ; syscall 93 = exit(0)
  ADDI  r0, r0, 0
  ADDI  r1, r0, 0
  ADDI  r15, r0, 0
  CALL  r14, 0

.section .rodata
msg:
  .ascii "Hello, Brad!\n"
```

Assemble and run:

```
bradc hello.basm -o hello.bvbc
bradvm --run hello.bvbc
```

Expected output:

```
Hello, Brad!
```

## 5 — Debug with bradgdb

```
bradgdb --run hello.bvbc
```

Common commands:

| Command | Action |
|---|---|
| `b 0x10` | set breakpoint at address 0x10 |
| `r` | continue / run |
| `s` | single-step one instruction |
| `i r` | show all registers |
| `m 0x0 32` | dump 32 bytes of memory from address 0x0 |
| `bt` | backtrace (show call stack via r14 chain) |
| `q` | quit |

`bradgdb` supports 64 hardware breakpoints — enough to instrument the entire branch table of a VSET kernel.

## 6 — Run the self-test suite

The repo includes a passing test harness that validates assembler round-trips, runtime behavior, and a 10-instruction `saxpy` kernel:

```
cd tests
make run-all
```

Every test should print `PASS`. If any print `FAIL`, check your build environment first.

## 7 — Use braddev (the all-in-one CLI)

`braddev` is the developer-facing wrapper around the whole toolchain.

```
cd site/js
braddev init                    # create project scaffold
braddev cvt hello.basm          # assemble → .bvbc
braddev run hello.bvbc          # run in BVRT
braddev dbg hello.bvbc          # launch bradgdb
braddev gpu info                 # show Torox GPU info (if hardware present)
braddev gpu top                  # live GPU utilisation
braddev timeline 65536 brad.events  # 65536-event timeline
braddev test                     # run full test suite
```

All commands accept `--help`.

## Next steps

- [Data model](02-data-model.md) — how memory and data are organised
- [Base ISA](04-base-isa.md) — the 16 instructions, one by one
- [ABI](05-abi.md) — calling convention, register roles, stack discipline
- [VSET](08-vector.md) — the vector / SIMD extension
