; SPDX-License-Identifier: MIT
; Tang Nano 9K boot ROM -- BradISA V1
;
; This is what the CPU on the board actually executes.  The board's 27 MHz
; oscillator would run it about 13 million times a second, so the board top
; divides the core clock down to one cycle per ~0.5 s and this program walks
; a counter on the 6 onboard LEDs via the data-memory write port.
;
;   addr  instruction                encoding      what it does
;   0x00  ADDI r1, r0, 0             0x81000000    init the counter
;   0x04  ADDI r1, r1, 1             0x81100001    r1++            <- loop head
;   0x08  ADDI r2, r0, 0x04          0x82000004    r2 = data address 0x04
;   0x0C  STW  r1, [r2 + 0]          0xA0210000    publish the counter
;   0x10  JMP  loop                  0xD000FFFC    back around
;
; The encodings in that table are not decoration: bradasm's self-test asserts
; each of these five words byte for byte, so the comment cannot drift away
; from what the assembler emits.  They were hand-assembled once, and that is
; exactly the practice this file exists to end.
;
; IMEM_WORDS is 16 on the board top, and the last 11 words are NOP-equivalents
; (ADDI r0, r0, 0), which the core decodes as a write to r0 and discards.
; Nothing ever fetches them -- the loop is closed by the JMP -- but a
; combinational ROM should not contain uninitialised bits either way.

        .org 0x00

        ADDI r1, r0, 0            ; init counter
loop:   ADDI r1, r1, 1            ; r1++
        ADDI r2, r0, 0x04        ; r2 = data address 0x04
        STW  r1, [r2 + 0]        ; publish counter out through dmem
        JMP  loop                ; back to the loop head

        .fill 11, 0x81000000     ; NOP-equivalent padding to 16 words

        .entry 0x00
