# BradISA Toolchain Guide

This repository ships the **BradISA specification and reference RTL only**. It does not bundle a toolchain or build binaries.

- For instruction encodings, see [Instruction formats](03-instruction-formats.md) and the normative LaTeX under `spec/`.
- For the reference assembler (`bradasm`) and emulator (`brad_core_emu`) interfaces, see [Toolchain and Reference Implementations](11-toolchain.md).
- For simulating the CPU RTL, see [FPGA](12-fpga.md).

The BradVector **GPU/accelerator** toolchain (`bradc`, BVRT, `bradgdb`, BradTimeline, `braddev`) is a separate ISA and is documented in the [BradVector repository](https://github.com/BradDevicesOfficial/BradVector).
