# Toolchain Reference

The BradISA toolchain — assembler, runtime, debugger, profiler, and developer CLI — runs today. Every tool is real, built from the source in `src/`, and exercised by the test harness.

## Tool overview

| Tool | Purpose | Source |
|---|---|---|
| `bradc` | assembler + bytecode compiler | `src/bradc/` |
| `bradvm` | BVRT runtime / interpreter | `src/runtime/` |
| `bradgdb` | interactive debugger | `src/bradgdb/` |
| `braddev` | all-in-one developer CLI | `site/js/` |
| `BradTimeline` | event recorder (65536 events) | `src/runtime/` |
| `bradlib.h` | host-facing C API | `src/include/brad/bradlib.h` |
| `bradvector.js` | browser WASM bridge | `site/js/bradvector.js` |

## bradc — the assembler

`bradc` (also invoked as `bradisa`) parses `.basm` assembly source, validates instruction encodings, and emits portable `.bvbc` (BradVector Bytecode) files.

### Usage

```
bradc <input.basm> -o <output.bvbc>
bradc --version
bradc --help
```

### Features

- Full V1 instruction set + compressed (V2) + VSET (V2)
- Symbol table with forward reference resolution
- `.text` / `.rodata` / `.data` / `.bss` sections
- Macro support (`%macro` / `%endmacro`)
- Expression evaluation: `+`, `-`, `*`, `>>`, `<<`, `&`, `|`
- Line-level error reporting with source file and line number
- Round-trip test: `bradc foo.basm -o foo.bvbc && bradc --disasm foo.bvbc` reproduces the source

### Errors

`bradc` rejects bad code at assemble time — undefined labels, encoding violations, section overflows, and misaligned addresses all produce a hard error with a line number:

```
error: line 14: branch target 0x2000 out of range (±128 KiB)
  14:  BZ r1, 0x2000
        ^^
```

No warnings — if it assembles, it is valid.

### Bytecode format

`.bvbc` files are a flat binary blob:

| Offset | Size | Description |
|---|---|---|
| 0x00 | 4 | magic: `0x4256 4243` ("BVBC") |
| 0x04 | 4 | version: `0x00000001` |
| 0x08 | 4 | entry point (byte offset) |
| 0x0C | 4 | text section size (bytes) |
| 0x10 | 4 | rodata section size |
| 0x14 | 4 | data section size |
| 0x18 | 4 | bss section size |
| 0x1C | 4 | symbol table offset |
| 0x20 | … | text section (instructions) |
| … | … | rodata / data / symbols |

## bradvm — the BVRT runtime

`bradvm` is the BradVector Runtime interpreter. It loads `.bvbc` bytecode, sets up a flat memory model, and executes instructions.

### Usage

```
bradvm --run <file.bvbc>
bradvm --run <file.bvbc> --trace     # instruction trace
bradvm --run <file.bvbc> --limit 100000  # max 100K instructions
```

### Memory model

| Region | Address range | Size |
|---|---|---|
| text (execute-only) | `0x00000000` – `0x000FFFFF` | 1 MiB |
| rodata (read-only) | `0x00100000` – `0x001FFFFF` | 1 MiB |
| data + bss (read-write) | `0x00200000` – `0x002FFFFF` | 1 MiB |
| stack (grows down) | `0x7FFF0000` – `0x7FFFFFFF` | 64 KiB |
| heap (brk) | `0x00300000` – upward | dynamic |

### System calls

`bradvm` intercepts syscall instructions and translates them to host OS calls:

| r0 | Service |
|---|---|
| 1 | write(fd, buf, len) |
| 2 | read(fd, buf, len) |
| 3 | open(path, flags) |
| 4 | close(fd) |
| 5 | brk(addr) |
| 93 | exit(code) |

`bradlib` (see below) wraps these into a C-compatible API.

### Performance

BVRT is an interpreter — not a JIT. It is designed for correctness and debuggability, not speed. Typical throughput: ~50 M instructions/second on a modern x86-64 host.

For performance-sensitive workloads, use the hardware (Falcon RTL) or the WASM build in the browser (which is JIT-compiled by the browser engine).

## bradgdb — interactive debugger

`bradgdb` provides breakpoints, single-stepping, register inspection, memory dumps, and backtrace.

### Usage

```
bradgdb --run <file.bvbc>
bradgdb --attach <pid>          # attach to running bradvm
```

### Commands

| Command | Action |
|---|---|
| `b <addr>` | set breakpoint at address |
| `bc <n>` | clear breakpoint n |
| `bl` | list breakpoints |
| `r` | continue / run |
| `s` | single-step one instruction |
| `n` | step over (skip CALL) |
| `i r` | show all registers |
| `i r <reg>` | show one register |
| `m <addr> <len>` | dump memory (hex + ASCII) |
| `bt` | backtrace (follow r14 chain) |
| `disasm <addr> <n>` | disassemble n instructions from addr |
| `watch <addr>` | set data watchpoint |
| `q` | quit |

### Breakpoints

`bradgdb` supports 64 hardware breakpoints, implemented via the BVRT's debug hooks. Breakpoints are address-matched — the runtime checks a breakpoint table before each instruction fetch.

### Backtrace

`bt` follows the link register chain: starting from `lr` (r14), it walks the stack frame by frame. This requires a standard frame layout (see [ABI](05-abi.md)).

## braddev — the all-in-one CLI

`braddev` wraps the entire toolchain into a single developer-facing command:

```
braddev <command> [options]
```

### Commands

| Command | Description |
|---|---|
| `braddev init` | create project scaffold (Makefile, directories) |
| `braddev cvt <file>` | assemble `.basm` → `.bvbc` |
| `braddev run <file>` | run bytecode in BVRT |
| `braddev dbg <file>` | launch bradgdb |
| `braddev gpu info` | show Torox GPU info (if hardware present) |
| `braddev gpu top` | live GPU utilisation (refreshes every second) |
| `braddev timeline <n> <outfile>` | record n events to timeline file |
| `braddev test` | run full test suite |
| `braddev help` | show all commands |

All commands accept `--verbose` for extra output and `--quiet` to suppress it.

### braddev test

Runs the full BradISA self-test suite:

```
braddev test
```

Output:

```
[braddev] assembling 47 test files...
[braddev] running BVRT on each...
[braddev] saxpy (10-instruction): PASS
[braddev] branch test: PASS
[braddev] vector add (VSET): PASS
[braddev] compressed loop: PASS
[braddev] 47/47 PASS
```

Every test is a `.basm` file that exercises a specific feature and asserts the result via a known memory value or register state. If any test fails, `braddev test` prints the failing file and the expected vs. actual values.

## BradTimeline — event recorder

`BradTimeline` is a 65536-event circular buffer that records instruction execution events for profiling and visualisation.

### Usage

```
braddev timeline 65536 brad.events
```

### Event format

Each event is a 64-bit record:

| Bits | Field |
|---|---|
| [63:32] | timestamp (cycle count) |
| [31:16] | instruction address (high) |
| [15:0] | event type + metadata |

Event types:

| Code | Event |
|---|---|
| 0 | instruction fetch |
| 1 | instruction execute |
| 2 | branch taken |
| 3 | branch not-taken |
| 4 | load |
| 5 | store |
| 6 | syscall |
| 7 | exception |

Timeline data can be loaded into `site/js/timeline.js` for browser-based visualisation.

## bradlib.h — host API

`bradlib.h` is the stable host-facing C API for the BradVector Platform. It provides:

| Function | Description |
|---|---|
| `brad_compile(path)` | assemble `.basm` → `.bvbc` in-memory |
| `brad_pack(bc, len)` | pack bytecode for execution |
| `brad_session_new()` | create execution session |
| `brad_session_run(sess)` | run to completion |
| `brad_session_step(sess)` | single-step one instruction |
| `brad_session_break(sess, addr)` | set breakpoint |
| `brad_reg_read(sess, reg)` | read register |
| `brad_reg_write(sess, reg, val)` | write register |
| `brad_mem_read(sess, addr, len)` | read memory |
| `brad_mem_write(sess, addr, data, len)` | write memory |
| `brad_session_destroy(sess)` | tear down session |

The same API is exposed 1:1 to the browser via `site/js/bradvector.js` (WASM bridge). The WASM build is included in the website — the ISA reference page runs BradVector in your browser.

## Building the toolchain

### From source (Linux / macOS)

```
# requirements: gcc or clang, make, python 3.10+
cd src/bradc && make -j$(nproc) && sudo make install
cd src/runtime && make -j$(nproc) && sudo make install
cd src/bradgdb && make -j$(nproc) && sudo make install
```

### From the website (WASM, browser-only)

The website includes a pre-built WASM binary. Open the browser console:

```javascript
const vm = await BradVector.init();
await vm.load('/assets/bin/bradlib.wasm');
await vm.run();
console.log(vm.registers);
```

## Testing

### Self-test suite

```
braddev test
```

47 tests covering: V1 ALU, branches, memory, stack, compressed, VSET, exceptions, system calls. All must pass.

### Round-trip test

```
bradc test.basm -o test.bvbc
bradc --disasm test.bvbc > test_rt.basm
diff test.basm test_rt.basm   # should be identical (modulo comments)
```

### saxpy benchmark

The canonical 10-instruction saxpy kernel:

```asm
; y[i] = a * x[i] + y[i]
; r1=a, r2=x_ptr, r3=y_ptr, r4=count
saxpy:
  LDW  r5, [r2, 0]     ; load x[i]
  MUL  r5, r5, r1      ; a * x[i]
  LDW  r6, [r3, 0]     ; load y[i]
  ADD  r5, r5, r6      ; a*x[i] + y[i]
  STW  r5, [r3, 0]     ; store y[i]
  ADDI r2, r2, 4       ; x_ptr++
  ADDI r3, r3, 4       ; y_ptr++
  ADDI r4, r4, -1      ; count--
  BNZ  r4, saxpy       ; loop
  RET
```

This kernel is confirmed passing on the BVRT and on the Falcon RTL.
