/* SPDX-License-Identifier: MIT */
/* brad_as self-test.
 *
 * Two kinds of check:
 *
 *  1. Encodings.  Every word asserted here was verified against the shipped
 *     RTL by simulation, not against this assembler's own opinion of itself.
 *     The values came out of the CI testbenches.
 *
 *  2. Drift.  The opcodes and register numbers are parsed straight out of
 *     rtl/verilog/bradisa_defines.v and compared against what brad_as.c
 *     believes.  A hand-maintained copy of an encoding table is a bug
 *     waiting for a Tuesday; this makes it a test failure instead.
 */

#include "brad_as.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int checks;
static int failures;

static void fail(const char *what, const char *detail)
{
    printf("FAIL %s: %s\n", what, detail);
    failures++;
}

static void expect_word(const char *what, const char *src, size_t index,
                        uint32_t want)
{
    struct brad_as_output *out;
    char detail[512];

    checks++;
    out = (struct brad_as_output *)malloc(sizeof *out);
    if (out == NULL) {
        printf("FAIL %s: out of memory\n", what);
        failures++;
        return;
    }
    if (brad_as_assemble(src, out) != 0) {
        snprintf(detail, sizeof detail, "assembly failed at line %u: %s",
                 out->err.line, out->err.error);
        fail(what, detail);
        free(out);
        return;
    }
    if (index >= out->code_words) {
        snprintf(detail, sizeof detail,
                 "asked for word %lu but only %lu were emitted",
                 (unsigned long)index, (unsigned long)out->code_words);
        fail(what, detail);
        free(out);
        return;
    }
    if (out->code[index] != want) {
        snprintf(detail, sizeof detail, "word %lu is 0x%08x, wanted 0x%08x",
                 (unsigned long)index, out->code[index], want);
        fail(what, detail);
    }
    free(out);
}

static void expect_error(const char *what, const char *src)
{
    struct brad_as_output *out;
    char detail[512];

    checks++;
    out = (struct brad_as_output *)malloc(sizeof *out);
    if (out == NULL) {
        printf("FAIL %s: out of memory\n", what);
        failures++;
        return;
    }
    if (brad_as_assemble(src, out) == 0) {
        fail(what, "assembled cleanly but should have been rejected");
        free(out);
        return;
    }
    if (out->err.error[0] == '\0') {
        fail(what, "rejected but with no error message");
    }
    snprintf(detail, sizeof detail, "rejected at line %u: %s", out->err.line,
             out->err.error);
    printf("     ok  %-34s %s\n", what, detail);
    free(out);
}

static void expect_ok(const char *what, const char *src)
{
    struct brad_as_output *out;
    char detail[512];

    checks++;
    out = (struct brad_as_output *)malloc(sizeof *out);
    if (out == NULL) {
        printf("FAIL %s: out of memory\n", what);
        failures++;
        return;
    }
    if (brad_as_assemble(src, out) != 0) {
        snprintf(detail, sizeof detail, "failed at line %u: %s", out->err.line,
                 out->err.error);
        fail(what, detail);
        free(out);
        return;
    }
    free(out);
}

static void expect_entry(const char *what, const char *src, uint32_t want)
{
    struct brad_as_output *out;
    char detail[512];

    checks++;
    out = (struct brad_as_output *)malloc(sizeof *out);
    if (out == NULL) {
        printf("FAIL %s: out of memory\n", what);
        failures++;
        return;
    }
    if (brad_as_assemble(src, out) != 0) {
        snprintf(detail, sizeof detail, "failed at line %u: %s", out->err.line,
                 out->err.error);
        fail(what, detail);
        free(out);
        return;
    }
    if (out->entry_point != want) {
        snprintf(detail, sizeof detail, "entry is 0x%08x, wanted 0x%08x",
                 out->entry_point, want);
        fail(what, detail);
    }
    free(out);
}

static void expect_sym(const char *what, const char *src, const char *name,
                       uint32_t want)
{
    struct brad_as_output *out;
    uint32_t got = 0;
    char detail[512];

    checks++;
    out = (struct brad_as_output *)malloc(sizeof *out);
    if (out == NULL) {
        printf("FAIL %s: out of memory\n", what);
        failures++;
        return;
    }
    if (brad_as_assemble(src, out) != 0) {
        snprintf(detail, sizeof detail, "failed at line %u: %s", out->err.line,
                 out->err.error);
        fail(what, detail);
        free(out);
        return;
    }
    if (!brad_as_lookup(out, name, &got)) {
        snprintf(detail, sizeof detail, "symbol '%s' not found", name);
        fail(what, detail);
    } else if (got != want) {
        snprintf(detail, sizeof detail, "'%s' is 0x%08x, wanted 0x%08x", name,
                 got, want);
        fail(what, detail);
    }
    free(out);
}

/* ─── Encodings, cross-checked against simulated RTL ──────────────────────
 *
 * These are the words the Verilog and VHDL testbenches were verified to
 * execute.  ADD/SUB/MUL/AND/OR/XOR/SHL/SHR especially: those are the eight
 * that were computing against r0 until tb_brad_isa caught it. */

static void test_encodings(void)
{
    printf("-- encodings (verified against simulated RTL)\n");

    expect_word("ADDI r2,r0,7",      "ADDI r2, r0, 7",      0, 0x82000007u);
    expect_word("ADDI r1,r0,7",      "ADDI r1, r0, 7",      0, 0x81000007u);
    expect_word("ADDI r3,r0,5",      "ADDI r3, r0, 5",      0, 0x83000005u);
    expect_word("ADD r1,r2,r3",      "ADD r1, r2, r3",      0, 0x01230000u);
    expect_word("STW [r2+8],r3",     "STW r3, [r2 + 8]",    0, 0xA0230008u);
    expect_word("LDW r4,[r2+8]",     "LDW r4, [r2 + 8]",    0, 0x94200008u);
    expect_word("STW alt spelling",  "STW r3, r2, 8",       0, 0xA0230008u);
    expect_word("LDW alt spelling",  "LDW r4, r2, 8",       0, 0x94200008u);
    expect_word("RET is PC<-lr",     "RET",                 0, 0xF0E00000u);
    expect_word("RET r0",            "RET r0",              0, 0xF0000000u);
    expect_word("RET r3",            "RET r3",              0, 0xF0300000u);
    expect_word("NOP",               "NOP",                 0, 0x80000000u);
    expect_word("case insensitive",  "aDdI r2, r0, 7",      0, 0x82000007u);
    expect_word("sp/lr aliases",     "ADDI sp, sp, 1\nADDI lr, lr, 2",
                0, 0x8DD00001u);
    expect_word("lr alias word 1",   "ADDI sp, sp, 1\nADDI lr, lr, 2",
                1, 0x8EE00002u);
    expect_word("negative immediate","ADDI r1, r0, -8",     0, 0x8100FFF8u);
    expect_word("immediate mask",    "ADDI r1, r0, 0x7F",   0, 0x8100007Fu);
    expect_word("MV r4,r5",            "MV r4, r5",            0, 0x84500000u);

    /* The four RRR opcodes, assembled rather than hand-encoded. */
    expect_word("SUB r1,r2,r3",      "SUB r1, r2, r3",      0, 0x11230000u);
    expect_word("MUL r1,r2,r3",      "MUL r1, r2, r3",      0, 0x21230000u);
    expect_word("AND r1,r2,r3",      "AND r1, r2, r3",      0, 0x31230000u);
    expect_word("OR r1,r2,r3",       "OR r1, r2, r3",       0, 0x41230000u);
    expect_word("XOR r1,r2,r3",      "XOR r1, r2, r3",      0, 0x51230000u);
    expect_word("SHL r1,r2,r3",      "SHL r1, r2, r3",      0, 0x61230000u);
    expect_word("SHR r1,r2,r3",      "SHR r1, r2, r3",      0, 0x71230000u);

    /* Branch / jump offsets are word counts relative to the *next*
     * instruction.  JMP self at 0x00 is the canonical -1 word. */
    expect_word("JMP self",          "JMP $",               0, 0xD000FFFFu);
    expect_word("JMP +2 words",      "here: JMP there\n"
                                      "     .word 0\n"
                                      "     .word 0\n"
                                      "there: NOP",         0, 0xD0000002u);
    expect_word("BZ r1,+1",          "BZ r1, there\n"
                                      "there: NOP",         0, 0xB0100000u);
    expect_word("BNZ r1,+1",         "BNZ r1, there\n"
                                      "there: NOP",         0, 0xC0100000u);
    expect_word("CALL named lr",     "CALL lr, there\n"
                                      "there: NOP",         0, 0xE0000000u);
    expect_word("CALL bare",         "CALL there\n"
                                      "there: NOP",         0, 0xE0000000u);

    /* Backward reference, i.e. a real loop. */
    expect_word("loop back",         "loop: ADDI r1, r1, -1\n"
                                      "      BNZ r1, loop",  1, 0xC010FFFEu);
}

/* ─── The board boot ROM, byte for byte ──────────────────────────────────
 *
 * rtl/fpga/tangnano9k/top_tangnano9k.v shipped these five words as hex
 * literals, hand-assembled.  If bradasm reproduces them exactly then the
 * board can be driven from source with zero behavioural change. */

static void test_boot_rom(void)
{
    static const char *const boot =
        "        .org 0x00\n"
        "        ADDI r1, r0, 0        ; init counter\n"
        "loop:   ADDI r1, r1, 1        ; r1++\n"
        "        ADDI r2, r0, 0x04     ; r2 = data address 0x04\n"
        "        STW  r1, [r2 + 0]     ; publish counter\n"
        "        JMP  loop             ; back around\n"
        "        .fill 11, 0x81000000  ; NOP-equivalent padding\n"
        "        .entry 0x00\n";

    printf("-- Tang Nano 9K boot ROM reproduces the hand-assembled words\n");

    expect_word("boot w0", boot, 0, 0x81000000u);
    expect_word("boot w1", boot, 1, 0x81100001u);
    expect_word("boot w2", boot, 2, 0x82000004u);
    expect_word("boot w3", boot, 3, 0xA0210000u);
    expect_word("boot w4", boot, 4, 0xD000FFFCu);
    expect_word("boot w5", boot, 5, 0x81000000u);
    expect_word("boot w15", boot, 15, 0x81000000u);
    expect_sym("boot loop label", boot, "loop", 0x04u);
    expect_entry("boot entry", boot, 0x00u);
}

/* ─── Symbols, expressions, directives ─────────────────────────────────── */

static void test_language(void)
{
    printf("-- symbols, expressions and directives\n");

    expect_sym("forward label",  "  JMP fwd\n  NOP\nfwd: NOP\n", "fwd", 0x08u);
    expect_sym("backward label",  "top: NOP\n  JMP top\n",       "top", 0x00u);
    expect_sym("equ constant",    ".equ K, 4\n ADDI r1, r0, K\n", "K", 4u);
    expect_sym("label on org",    ".org 0x40\nhere: NOP\n",      "here", 0x40u);
    expect_sym("two labels/line", "a: b: NOP\n",                "a", 0x00u);
    expect_sym("two labels/line", "a: b: NOP\n",                "b", 0x00u);

    expect_word("hex binary lit", "ADDI r1, r0, 0b1010", 0, 0x8100000Au);
    expect_word("underscores",    "ADDI r1, r0, 0x7F_FF", 0, 0x81007FFFu);
    expect_word("char literal",   "ADDI r1, r0, 'A'",    0, 0x81000041u);
    expect_word("dollar is PC",   "  ADDI r1, r0, $\n",  0, 0x81000000u);
    expect_word("dot is PC too",  "  ADDI r1, r0, . + 4\n", 0, 0x81000004u);
    expect_word("operator prec",  "ADDI r1, r0, 1 + 2 * 3", 0, 0x81000007u);
    expect_word("parens",         "ADDI r1, r0, (1 + 2) * 3", 0, 0x81000009u);
    expect_word("bitwise",        "ADDI r1, r0, 0xF0 | 0x0F", 0, 0x810000FFu);
    expect_word("shifts",         "ADDI r1, r0, 1 << 8",  0, 0x81000100u);
    expect_word("unary minus",    "ADDI r1, r0, -(-5)", 0, 0x81000005u);
    expect_word("not",            "ADDI r1, r0, ~0 & 0xF", 0, 0x8100000Fu);
    expect_word("comments",       "; a comment\n ADDI r1, r0, 1 ; trailing\n",
                0, 0x81000001u);
    expect_word("blank lines",    "\n\n\n   ADDI r1, r0, 1\n\n",
                0, 0x81000001u);

    /* A computed offset, which is the feature that makes a table position
     * independent. */
    expect_word("self-relative size",
                "start: NOP\n NOP\n NOP\n .word end - start\n"
                "end: NOP\n", 3, 0x0000000Cu);

    /* .byte / .half pack little-endian into the word image. */
    expect_word("byte order", ".byte 0x11, 0x22, 0x33, 0x44\n", 0,
                0x44332211u);
    expect_word("half order", ".half 0x1122\n .half 0x3344\n", 0,
                0x33441122u);
    expect_word("mixed data",  ".byte 1\n .half 2\n .byte 3\n", 0,
                0x03000201u);
}

/* ─── Errors that must be caught ───────────────────────────────────────── */

static void test_errors(void)
{
    printf("-- diagnostics\n");

    expect_error("unknown mnemonic",      "FLY r1, r2\n");
    expect_error("unknown directive",     ".frobnicate 3\n");
    expect_error("not a register",        "ADDI r16, r0, 1\n");
    expect_error("not a register (text)", "ADDI banana, r0, 1\n");
    expect_error("missing comma",         "ADDI r1 r0 1\n");
    expect_error("too few operands",      "ADD r1, r2\n");
    expect_error("too many operands",     "ADD r1, r2, r3, r4\n");
    expect_error("immediate too large",   "ADDI r1, r0, 40000\n");
    expect_error("immediate too small",   "ADDI r1, r0, -40000\n");
    expect_error("undefined label",       "JMP nowhere\n");
    expect_error("unaligned .org",        ".org 2\n  NOP\n");
    expect_error("out of reach",          "JMP 0x40000\n");
    expect_error("label defined twice",   "x: NOP\nx: NOP\n");
    expect_error("NOP with operands",     "NOP r1\n");
    expect_error("CALL non-lr link",      "CALL r3, $ + 4\n");
    expect_error("compressed rejected",   "C.ADDI r1, 1\n");
    expect_error("compressed rejected 2", "C.NOP\n");
    /* The line number must point at the offending line, not line 1. */
    {
        struct brad_as_output *out = (struct brad_as_output *)malloc(1 << 20);
        checks++;
        if (out == NULL) {
            printf("FAIL error line number: out of memory\n");
            failures++;
        } else {
            static const char *src =
                "NOP\nNOP\nNOP\nBOGUS r1\n";
            if (brad_as_assemble(src, out) == 0) {
                fail("error line number", "bad source assembled cleanly");
            } else if (out->err.line != 4) {
                char d[256];
                snprintf(d, sizeof d, "error reported on line %u, wanted 4",
                         out->err.line);
                fail("error line number", d);
            }
            free(out);
        }
    }
}

/* ─── Drift guard against rtl/verilog/bradisa_defines.v ────────────────── */

/* Parse a Verilog constant: 4'h0, 4'hA, 5'd31, 24'd1_048_575, or a plain
 * 1234.  The RTL writes all six of those shapes, so this has to read all
 * six -- a drift guard that quietly skips half the file is not a guard. */
static int verilog_value(const char *s, unsigned long *out)
{
    const char *q;
    unsigned long v = 0;
    int base = 10;
    int saw_base = 0;

    while (*s == ' ' || *s == '\t')
        s++;

    for (q = s; *q; q++) {
        if (*q != '\'')
            continue;
        if (q[1] == 'h' || q[1] == 'H') { base = 16; saw_base = 1; }
        else if (q[1] == 'b' || q[1] == 'B') { base = 2;  saw_base = 1; }
        else if (q[1] == 'd' || q[1] == 'D') { base = 10; saw_base = 1; }
        if (saw_base)
            break;
    }

    if (saw_base)
        s = q + 2;
    for (; *s; s++) {
        int d;
        if (*s == '_' || *s == ';' || *s == ' ' || *s == '\t' || *s == '\r' ||
            *s == '\n' || *s == ',')
            continue;
        d = (unsigned char)*s;
        if (d >= '0' && d <= '9')
            d -= '0';
        else if (d >= 'A' && d <= 'F')
            d = d - 'A' + 10;
        else if (d >= 'a' && d <= 'f')
            d = d - 'a' + 10;
        else
            return 0;
        if (d >= base)
            return 0;
        v = v * (unsigned long)base + (unsigned long)d;
    }
    *out = v;
    return 1;
}

static int read_define(const char *path, const char *name, unsigned long *val)
{
    FILE *f = fopen(path, "r");
    char line[256];
    int found = 0;

    if (f == NULL)
        return 0;
    while (fgets(line, sizeof line, f) != NULL) {
        char key[64];
        const char *eq;
        if (sscanf(line, "localparam %63s", key) != 1)
            continue;
        if (strcmp(key, name) != 0)
            continue;
        eq = strchr(line, '=');
        if (eq != NULL && verilog_value(eq + 1, val))
            found = 1;
        break;
    }
    fclose(f);
    return found;
}

static void expect_define(const char *path, const char *name, uint32_t want)
{
    unsigned long got = 0;

    checks++;
    if (!read_define(path, name, &got)) {
        char d[256];
        snprintf(d, sizeof d, "%s not found in %s", name, path);
        printf("SKIP %s\n", d);
        checks--;
        return;
    }
    if ((uint32_t)got != want) {
        char d[256];
        snprintf(d, sizeof d, "%s is 0x%lx in the RTL, 0x%08x in brad_as.c",
                 name, got, want);
        fail("ISA constant drift", d);
    }
}

static void test_defines_drift(const char *defines_path)
{
    printf("-- opcode table matches %s\n", defines_path);

    expect_define(defines_path, "BRAD_OP_ADD",  0x0u);
    expect_define(defines_path, "BRAD_OP_SUB",  0x1u);
    expect_define(defines_path, "BRAD_OP_MUL",  0x2u);
    expect_define(defines_path, "BRAD_OP_AND",  0x3u);
    expect_define(defines_path, "BRAD_OP_OR",   0x4u);
    expect_define(defines_path, "BRAD_OP_XOR",  0x5u);
    expect_define(defines_path, "BRAD_OP_SHL",  0x6u);
    expect_define(defines_path, "BRAD_OP_SHR",  0x7u);
    expect_define(defines_path, "BRAD_OP_ADDI", 0x8u);
    expect_define(defines_path, "BRAD_OP_LDW",  0x9u);
    expect_define(defines_path, "BRAD_OP_STW",  0xAu);
    expect_define(defines_path, "BRAD_OP_BZ",   0xBu);
    expect_define(defines_path, "BRAD_OP_BNZ",  0xCu);
    expect_define(defines_path, "BRAD_OP_JMP",  0xDu);
    expect_define(defines_path, "BRAD_OP_CALL", 0xEu);
    expect_define(defines_path, "BRAD_OP_RET",  0xFu);
    expect_define(defines_path, "BRAD_LR",      14u);
    expect_define(defines_path, "BRAD_SP",      13u);
    expect_define(defines_path, "BRAD_R0",      0u);
}

int main(int argc, char **argv)
{
    const char *defines = (argc > 1) ? argv[1]
                                     : "../../rtl/verilog/bradisa_defines.v";

    printf("bradasm self-test\n");

    test_encodings();
    test_boot_rom();
    test_language();
    test_errors();
    test_defines_drift(defines);

    /* A file that must assemble cleanly, to make sure the "no errors" path is
     * actually exercised somewhere. */
    expect_ok("full program",
        ".equ  BASE, 0x40\n"
        ".org 0x00\n"
        "start: ADDI r1, r0, 0\n"
        "       ADDI r2, r0, BASE\n"
        "loop:  ADDI r1, r1, 1\n"
        "       STW  r1, [r2 + 0]\n"
        "       BZ  r1, done\n"
        "       JMP loop\n"
        "done:  BZ  r1, sub\n"
        "       RET\n"
        "sub:   ADDI r3, r0, 1\n"
        "       RET\n"
        ".entry start\n");

    printf("\nchecks run : %d\n", checks);
    printf("failures   : %d\n", failures);
    if (failures == 0) {
        printf("PASS\n");
        return 0;
    }
    printf("FAIL\n");
    return 1;
}
