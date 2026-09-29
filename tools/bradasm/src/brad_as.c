/* SPDX-License-Identifier: MIT */
/* bradasm -- the BradISA assembler.
 *
 * Two passes.  Pass 1 walks the source to learn every label address; pass 2
 * walks it again to encode.  Every instruction bradasm emits is exactly one
 * 32-bit word, so pass 1 never needs to evaluate a branch target -- it only
 * needs to know how wide each line is.  That is what makes a two-pass
 * assembler with automatic forward references this short.
 *
 * The image is assembled as bytes (so .byte and .half work at any
 * alignment) in the same storage that later holds the words, then packed
 * little-endian into uint32_t on the way out.
 */

#include "brad_as.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ─── ISA constants, mirroring rtl/verilog/bradisa_defines.v ───────────
 *
 * Kept as literals rather than generated from the .v file so the assembler
 * builds with nothing but a C compiler.  tests/test_brad_as.c parses
 * bradisa_defines.v and asserts these match, so the two cannot drift. */

#define OP_ADD   0x0u
#define OP_SUB   0x1u
#define OP_MUL   0x2u
#define OP_AND   0x3u
#define OP_OR    0x4u
#define OP_XOR   0x5u
#define OP_SHL   0x6u
#define OP_SHR   0x7u
#define OP_ADDI  0x8u
#define OP_LDW   0x9u
#define OP_STW   0xAu
#define OP_BZ    0xBu
#define OP_BNZ   0xCu
#define OP_JMP   0xDu
#define OP_CALL  0xEu
#define OP_RET   0xFu

#define REG_LR   14u
#define NUM_REGS 16u

/* The byte-addressable window the assembler writes through.  256 KiB, which
 * is BRAD_AS_MAX_WORDS words. */
#define IMAGE_BYTES (BRAD_AS_MAX_WORDS * 4u)

/* Word layout: [31:28] op | [27:24] rd | [23:20] rs1 | [19:16] rs2 | imm16 */
#define ENC(op, rd, rs1, rs2, imm)                                        \
    ((uint32_t)(op) << 28 | (uint32_t)(rd) << 24 |                         \
     (uint32_t)(rs1) << 20 | (uint32_t)(rs2) << 16 |                       \
     ((uint32_t)(imm) & 0xFFFFu))

/* Truncate to a 16-bit two's-complement field. */
#define IMM16(v) ((uint32_t)(int32_t)(uint16_t)(int16_t)(v))

#define IMM16_MIN (-32768)
#define IMM16_MAX (32767)

/* BZ/BNZ/JMP/CALL carry a 16-bit *word* offset, so +/-128 KiB. */
#define WORD_OFF_MIN (-32768)
#define WORD_OFF_MAX (32767)

/* ─── Lexer helpers ──────────────────────────────────────────────────── */

static int is_space(int c) { return c == ' ' || c == '\t' || c == '\r'; }

static int is_digit(int c) { return c >= '0' && c <= '9'; }

static int is_alpha(int c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

static int is_alnum(int c)
{
    return is_alpha(c) || is_digit(c) || c == '.' || c == '$';
}

static int lower(int c) { return (c >= 'A' && c <= 'Z') ? c - 'A' + 'a' : c; }

static int ci_eqn(const char *a, size_t alen, const char *b)
{
    size_t i;

    for (i = 0; i < alen; i++) {
        if (b[i] == '\0')
            return 0;
        if (lower((unsigned char)a[i]) != (unsigned char)b[i])
            return 0;
    }
    return b[alen] == '\0';
}

/* ─── Assembler context ──────────────────────────────────────────────── */

/* Whether an operand value was written out or came from a label.  Only used
 * to phrase an out-of-range diagnostic sensibly. */
enum val_kind { VAL_PLAIN, VAL_SYMBOL };

struct as_ctx {
    const char            *p;        /* cursor */
    unsigned               line;     /* 1-based */
    struct brad_as_output *out;
    unsigned               pass;
    int                    unresolved; /* a label was not known yet */

    /* Pass 1's complete label map.  Both passes resolve against this, so a
     * forward reference works in pass 2.  out->symbols is only written in
     * pass 2 -- otherwise every label would look like a duplicate. */
    struct brad_as_symbol *labels;
    size_t                 label_count;

    /* Optional listing, filled in pass 2 only. */
    struct brad_as_listing *lst;
    size_t                  lst_n;
    size_t                  lst_cap;

    /* per-line state */
    uint32_t               addr;     /* address of the instruction being encoded */
    enum val_kind          kind;
};

static void as_error(struct as_ctx *c, const char *fmt, ...);

static void as_error(struct as_ctx *c, const char *fmt, ...)
{
    va_list ap;

    /* Pass 1 only builds the label map; its diagnostics are noise because
     * forward references are legitimate there.  Pass 2 is authoritative and
     * keeps the first error, since later ones are usually fallout. */
    if (c->pass != 2 || c->out->err.ok == 0)
        return;
    c->out->err.ok = 0;
    c->out->err.line = c->line;
    va_start(ap, fmt);
    vsnprintf(c->out->err.error, BRAD_AS_ERR_LEN, fmt, ap);
    va_end(ap);
}

/* ─── Listing ─────────────────────────────────────────────────────────── */

static void listing_push(struct as_ctx *c, uint32_t addr, uint32_t word,
                         int has_word)
{
    if (c->lst_n < c->lst_cap) {
        c->lst[c->lst_n].line     = c->line;
        c->lst[c->lst_n].addr     = addr;
        c->lst[c->lst_n].word     = word;
        c->lst[c->lst_n].has_word = has_word;
    }
    c->lst_n++;
}

/* ─── Symbol table ─────────────────────────────────────────────────────
 *
 * Two tables, on purpose.  `c->labels` is pass 1's complete map: every label
 * the program defines, built before any code is encoded, so pass 2 can
 * resolve a forward reference.  `out->symbols` is the user-facing table and
 * is written only in pass 2, so it lists the program once. */

static int sym_lookup(struct as_ctx *c, const char *name, size_t len,
                      uint32_t *addr)
{
    const struct brad_as_symbol *t = c->labels;
    size_t n = c->label_count;
    size_t i;

    for (i = 0; i < n; i++) {
        if (strlen(t[i].name) == len && memcmp(t[i].name, name, len) == 0) {
            *addr = t[i].addr;
            return 1;
        }
    }
    return 0;
}

static void sym_define(struct as_ctx *c, const char *name, size_t len,
                       uint32_t addr)
{
    struct brad_as_symbol *s;
    char tmp[BRAD_AS_SYM_LEN];
    uint32_t scratch;
    size_t k, n;

    if (len >= BRAD_AS_SYM_LEN)
        len = BRAD_AS_SYM_LEN - 1;

    if (c->pass == 1) {
        /* The shadow map is built by the same walk that fills it, so a
         * duplicate is a pass-2 report.  Pass 1 just keeps the first. */
        if (sym_lookup(c, name, len, &scratch) ||
            c->label_count >= BRAD_AS_MAX_SYMS) {
            if (c->label_count >= BRAD_AS_MAX_SYMS)
                as_error(c, "too many symbols (limit %u)",
                         (unsigned)BRAD_AS_MAX_SYMS);
            return;
        }
        s = &c->labels[c->label_count++];
        memcpy(s->name, name, len);
        s->name[len] = '\0';
        s->addr = addr;
        return;
    }

    if (c->out->symbol_count >= BRAD_AS_MAX_SYMS) {
        as_error(c, "too many symbols (limit %u)", (unsigned)BRAD_AS_MAX_SYMS);
        return;
    }
    n = len < sizeof tmp - 1 ? len : sizeof tmp - 1;
    memcpy(tmp, name, n);
    tmp[n] = '\0';
    for (k = 0; k < c->out->symbol_count; k++) {
        if (strcmp(c->out->symbols[k].name, tmp) == 0) {
            as_error(c, "symbol '%s' is defined twice", tmp);
            return;
        }
    }
    s = &c->out->symbols[c->out->symbol_count++];
    memcpy(s->name, name, len);
    s->name[len] = '\0';
    s->addr = addr;
}

/* ─── Emitter ────────────────────────────────────────────────────────── */

static void emit_byte(struct as_ctx *c, uint32_t addr, uint8_t b)
{
    uint8_t *img = (uint8_t *)c->out->code;

    if (addr >= IMAGE_BYTES) {
        as_error(c, "address 0x%08x is past the assembler's %u KiB image "
                    "window", addr, (unsigned)(IMAGE_BYTES / 1024u));
        return;
    }
    img[addr] = b;
    if (addr < c->out->base_addr)
        c->out->base_addr = addr;
    if (addr + 1u > c->out->limit_addr)
        c->out->limit_addr = addr + 1u;
}

static void emit_word(struct as_ctx *c, uint32_t addr, uint32_t word)
{
    emit_byte(c, addr,     (uint8_t)(word >> 0));
    emit_byte(c, addr + 1, (uint8_t)(word >> 8));
    emit_byte(c, addr + 2, (uint8_t)(word >> 16));
    emit_byte(c, addr + 3, (uint8_t)(word >> 24));
}

/* ─── Expression evaluator ─────────────────────────────────────────────
 *
 * Precedence, loosest first:  |  ^  &  << >>  + -  * / %  unary  primary
 */

static int parse_expr(struct as_ctx *c, int64_t *out_val);

static int64_t parse_primary(struct as_ctx *c, int64_t *out_val)
{
    int ch;

    if (c->unresolved)
        return 0;

    while (is_space((unsigned char)*c->p))
        c->p++;

    ch = (unsigned char)*c->p;

    /* `$` and `.` both mean "the address of this instruction".  That is what
     * makes a position-independent table like `.word end - start` work, and
     * it is the one feature here that earns its keep. */
    if (ch == '$' || (ch == '.' && !is_digit((unsigned char)c->p[1]))) {
        c->p++;
        *out_val = (int64_t)c->addr;
        return 1;
    }

    if (ch == '(') {
        int64_t v;
        c->p++;
        if (!parse_expr(c, &v))
            return 0;
        while (is_space((unsigned char)*c->p))
            c->p++;
        if (*c->p != ')') {
            as_error(c, "expected ')' to close an expression");
            return 0;
        }
        c->p++;
        *out_val = v;
        return 1;
    }

    if (ch == '-') {
        int64_t v;
        c->p++;
        if (!parse_primary(c, &v))
            return 0;
        *out_val = -v;
        return 1;
    }
    if (ch == '+') {
        c->p++;
        return parse_primary(c, out_val);
    }
    if (ch == '~') {
        int64_t v;
        c->p++;
        if (!parse_primary(c, &v))
            return 0;
        *out_val = ~v;
        return 1;
    }

    if (ch == '\'') {                     /* character literal */
        int64_t v = 0;
        c->p++;
        if (*c->p == '\\' && c->p[1] != '\0') {
            c->p++;
            switch (*c->p) {
            case 'n':  v = '\n'; break;
            case 'r':  v = '\r'; break;
            case 't':  v = '\t'; break;
            case '0':  v = '\0'; break;
            case '\\': v = '\\'; break;
            case '\'': v = '\''; break;
            default:   v = (unsigned char)*c->p; break;
            }
            c->p++;
        } else if (*c->p != '\0' && *c->p != '\n' && *c->p != '\'') {
            v = (unsigned char)*c->p;
            c->p++;
        } else {
            as_error(c, "empty character literal");
            return 0;
        }
        if (*c->p != '\'') {
            as_error(c, "unterminated character literal");
            return 0;
        }
        c->p++;
        *out_val = v;
        return 1;
    }

    if (is_digit(ch)) {
        uint64_t v = 0;
        int base = 10;

        if (ch == '0' && (c->p[1] == 'x' || c->p[1] == 'X')) {
            base = 16;
            c->p += 2;
        } else if (ch == '0' && (c->p[1] == 'b' || c->p[1] == 'B')) {
            base = 2;
            c->p += 2;
        } else if (ch == '0' && (c->p[1] == 'o' || c->p[1] == 'O')) {
            base = 8;
            c->p += 2;
        }

        while (is_alnum((unsigned char)*c->p) || *c->p == '_') {
            int d;
            if (*c->p == '_') {           /* 0x8100_0000 beats 0x81000000 */
                c->p++;
                continue;
            }
            d = lower((unsigned char)*c->p);
            if (d >= '0' && d <= '9')
                d -= '0';
            else if (d >= 'a' && d <= 'f')
                d = d - 'a' + 10;
            else
                break;
            if (d >= base) {
                as_error(c, "'%c' is not a valid digit in base %d", *c->p, base);
                return 0;
            }
            v = v * (uint64_t)base + (uint64_t)d;
            c->p++;
        }
        if (is_alpha((unsigned char)*c->p)) {
            as_error(c, "malformed number");
            return 0;
        }
        *out_val = (int64_t)v;
        c->kind = VAL_PLAIN;
        return 1;
    }

    if (is_alpha(ch)) {
        const char *start = c->p;
        size_t len;
        uint32_t addr;

        while (is_alnum((unsigned char)*c->p))
            c->p++;
        len = (size_t)(c->p - start);
        c->kind = VAL_SYMBOL;

        if (sym_lookup(c, start, len, &addr)) {
            *out_val = (int64_t)addr;
            return 1;
        }
        /* Unknown in pass 1 means a forward reference, which is fine.  Pass 2
         * is where an actually-undefined symbol gets reported. */
        c->unresolved = 1;
        *out_val = 0;
        return 1;
    }

    if (ch == '\0' || ch == '\n') {
        as_error(c, "expected an expression");
    } else {
        as_error(c, "unexpected character '%c' in expression", ch);
    }
    return 0;
}

static int parse_mul(struct as_ctx *c, int64_t *out_val)
{
    int64_t lhs;

    if (!parse_primary(c, &lhs))
        return 0;
    for (;;) {
        int op;
        int64_t rhs;

        while (is_space((unsigned char)*c->p))
            c->p++;
        op = *c->p;
        if (op != '*' && op != '/' && op != '%')
            break;
        c->p++;
        if (!parse_primary(c, &rhs))
            return 0;
        if (c->unresolved) {
            lhs = 0;
        } else if (op == '*') {
            lhs *= rhs;
        } else if (rhs == 0) {
            as_error(c, "division by zero");
            return 0;
        } else if (op == '/') {
            lhs /= rhs;
        } else {
            lhs %= rhs;
        }
    }
    *out_val = lhs;
    return 1;
}

static int parse_add(struct as_ctx *c, int64_t *out_val)
{
    int64_t lhs;

    if (!parse_mul(c, &lhs))
        return 0;
    for (;;) {
        int op;
        int64_t rhs;

        while (is_space((unsigned char)*c->p))
            c->p++;
        op = *c->p;
        if (op != '+' && op != '-')
            break;
        c->p++;
        if (!parse_mul(c, &rhs))
            return 0;
        lhs = c->unresolved ? 0 : (op == '+' ? lhs + rhs : lhs - rhs);
    }
    *out_val = lhs;
    return 1;
}

static int parse_shift(struct as_ctx *c, int64_t *out_val)
{
    int64_t lhs;

    if (!parse_add(c, &lhs))
        return 0;
    for (;;) {
        int is_left;
        int64_t rhs;

        while (is_space((unsigned char)*c->p))
            c->p++;
        if (c->p[0] == '<' && c->p[1] == '<')
            is_left = 1;
        else if (c->p[0] == '>' && c->p[1] == '>')
            is_left = 0;
        else
            break;
        c->p += 2;
        if (!parse_add(c, &rhs))
            return 0;
        if (!c->unresolved) {
            if (rhs < 0 || rhs > 63) {
                as_error(c, "shift count %lld is out of range (0..63)",
                         (long long)rhs);
                return 0;
            }
            lhs = is_left ? (lhs << rhs) : (lhs >> rhs);
        } else {
            lhs = 0;
        }
    }
    *out_val = lhs;
    return 1;
}

static int parse_bitand(struct as_ctx *c, int64_t *out_val)
{
    int64_t lhs;

    if (!parse_shift(c, &lhs))
        return 0;
    while (c->p[0] == '&' && c->p[1] != '&') {
        int64_t rhs;
        c->p++;
        if (!parse_shift(c, &rhs))
            return 0;
        lhs = c->unresolved ? 0 : (lhs & rhs);
    }
    *out_val = lhs;
    return 1;
}

static int parse_bitxor(struct as_ctx *c, int64_t *out_val)
{
    int64_t lhs;

    if (!parse_bitand(c, &lhs))
        return 0;
    while (c->p[0] == '^') {
        int64_t rhs;
        c->p++;
        if (!parse_bitand(c, &rhs))
            return 0;
        lhs = c->unresolved ? 0 : (lhs ^ rhs);
    }
    *out_val = lhs;
    return 1;
}

static int parse_expr(struct as_ctx *c, int64_t *out_val)
{
    int64_t lhs;

    if (!parse_bitxor(c, &lhs))
        return 0;
    while (c->p[0] == '|') {
        int64_t rhs;
        c->p++;
        if (!parse_bitxor(c, &rhs))
            return 0;
        lhs = c->unresolved ? 0 : (lhs | rhs);
    }
    *out_val = lhs;
    return 1;
}

/* Start an expression context: "we are about to evaluate a fresh operand,
 * so nothing is known yet". */
static void expr_begin(struct as_ctx *c)
{
    c->unresolved = 0;
    c->kind = VAL_PLAIN;
}

/* ─── Token / operand helpers ────────────────────────────────────────── */

static void skip_spaces(struct as_ctx *c)
{
    while (is_space((unsigned char)*c->p))
        c->p++;
}

static int at_eol(struct as_ctx *c)
{
    skip_spaces(c);
    return *c->p == '\0' || *c->p == '\n' || *c->p == ';';
}

static int expect_comma(struct as_ctx *c)
{
    skip_spaces(c);
    if (*c->p == ',') {
        c->p++;
        return 1;
    }
    as_error(c, "expected ',' between operands");
    return 0;
}

static int reg_name(const char *s, size_t len, uint32_t *num)
{
    static const char *const alias[] = { "zero", "sp", "lr", "pc" };
    static const uint32_t alias_num[] = { 0u, 13u, 14u, 15u };
    size_t i, j, k;
    uint32_t n = 0;

    /* r0 .. r15, with or without the leading 'r' */
    j = (len > 0 && (s[0] == 'r' || s[0] == 'R')) ? 1u : 0u;
    if (j < len && is_digit((unsigned char)s[j])) {
        for (; j < len; j++) {
            if (!is_digit((unsigned char)s[j]))
                return 0;
            n = n * 10u + (uint32_t)(s[j] - '0');
            if (n >= NUM_REGS)
                return 0;
        }
        *num = n;
        return 1;
    }

    for (i = 0; i < sizeof alias / sizeof alias[0]; i++) {
        size_t alen = strlen(alias[i]);
        if (alen != len)
            continue;
        for (k = 0; k < len; k++)
            if (lower((unsigned char)s[k]) != (unsigned char)alias[i][k])
                break;
        if (k == len) {
            *num = alias_num[i];
            return 1;
        }
    }
    return 0;
}

/* Is the next token a register?  Consumes it and reports which, silently.
 * Used where a register is optional (CALL's link register) and where a
 * failure should not produce a diagnostic. */
static int peek_reg(struct as_ctx *c, uint32_t *num)
{
    const char *save = c->p;
    const char *start;
    size_t len;
    int ok;

    skip_spaces(c);
    start = c->p;
    while (is_alnum((unsigned char)*c->p))
        c->p++;
    len = (size_t)(c->p - start);
    ok = len > 0 && reg_name(start, len, num);
    if (!ok)
        c->p = save;
    return ok;
}

static int parse_reg(struct as_ctx *c, uint32_t *num, const char *what)
{
    const char *start;
    size_t len;

    skip_spaces(c);
    start = c->p;
    while (is_alnum((unsigned char)*c->p))
        c->p++;
    len = (size_t)(c->p - start);
    if (len == 0) {
        as_error(c, "expected a register for %s", what);
        return 0;
    }
    if (!reg_name(start, len, num)) {
        char tmp[32];
        size_t n = len < sizeof tmp - 1 ? len : sizeof tmp - 1;
        memcpy(tmp, start, n);
        tmp[n] = '\0';
        as_error(c, "'%s' is not a register (expected r0-r15, sp, lr or pc)",
                 tmp);
        return 0;
    }
    return 1;
}

static int check_imm16(struct as_ctx *c, int64_t v, const char *what)
{
    if (c->unresolved) {
        /* Pass 1's forward references are normal; pass 2's are typos. */
        if (c->pass == 2)
            as_error(c, "undefined symbol used as %s", what);
        return 1;
    }
    if (v >= IMM16_MIN && v <= IMM16_MAX)
        return 1;
    if (c->kind == VAL_SYMBOL)
        as_error(c, "%s resolves to 0x%llx, which is out of reach for a "
                    "16-bit signed immediate", what,
                 (unsigned long long)(uint32_t)v);
    else
        as_error(c, "%s is %lld, which does not fit in a 16-bit signed "
                    "immediate (%d..%d)", what, (long long)v,
                 IMM16_MIN, IMM16_MAX);
    return 0;
}

/* ─── Address modes for LDW / STW ────────────────────────────────────── */

/* Accepts both documented spellings:
 *     LDW r1, [r2 + 4]
 *     LDW r1, r2, 4
 * and the bare-base form `LDW r1, r2`. */
static int parse_mem(struct as_ctx *c, uint32_t *base, int64_t *imm_out)
{
    skip_spaces(c);

    if (*c->p == '[') {
        c->p++;
        if (!parse_reg(c, base, "the base register"))
            return 0;
        skip_spaces(c);
        if (*c->p != ']') {
            int64_t off = 0;
            expr_begin(c);
            if (!parse_expr(c, &off))
                return 0;
            *imm_out = off;
        }
        skip_spaces(c);
        if (*c->p != ']') {
            as_error(c, "expected ']' to close the memory operand");
            return 0;
        }
        c->p++;
        return 1;
    }

    if (!parse_reg(c, base, "the base register"))
        return 0;
    skip_spaces(c);
    if (*c->p == ',') {
        int64_t off = 0;
        c->p++;
        expr_begin(c);
        if (!parse_expr(c, &off))
            return 0;
        *imm_out = off;
    }
    return 1;
}

/* ─── Relative target for BZ / BNZ / JMP / CALL ───────────────────────── */

/* The RTL computes `target = pc + 4 + sext(offset) * 4`, so the stored field
 * is a word count relative to the *next* instruction.  Being off by a factor
 * of four here is the single most common way to hand-assemble this ISA
 * wrong, which is why it lives in exactly one place. */
static int rel_target(struct as_ctx *c, int64_t *off_out)
{
    int64_t target = 0;
    int64_t delta;

    expr_begin(c);
    if (!parse_expr(c, &target))
        return 0;
    if (c->unresolved) {
        if (c->pass == 2)
            as_error(c, "undefined symbol used as a branch or call target");
        *off_out = 0;        /* pass 1; pass 2 resolves or reports it */
        return 1;
    }
    delta = target - ((int64_t)c->addr + 4);
    if (delta % 4 != 0) {
        as_error(c, "target 0x%llx is not word-aligned relative to this "
                    "instruction (delta %lld)", (unsigned long long)target,
                 (long long)delta);
        return 0;
    }
    delta /= 4;
    if (delta < WORD_OFF_MIN || delta > WORD_OFF_MAX) {
        as_error(c, "target 0x%llx is out of reach: this is a 16-bit word "
                    "offset, so branches and calls span only +/-128 KiB",
                 (unsigned long long)target);
        return 0;
    }
    *off_out = delta;
    return 1;
}

/* ─── Mnemonics ──────────────────────────────────────────────────────── */

enum mnemonic {
    M_NONE, M_RRR, M_ADDI, M_LDW, M_STW, M_BZ, M_BNZ, M_JMP, M_CALL, M_RET
};

struct mnem_entry {
    const char *name;
    enum mnemonic m;
    uint32_t     op;
};

static const struct mnem_entry mnemonics[] = {
    { "add",  M_RRR,  OP_ADD  }, { "sub",  M_RRR,  OP_SUB  },
    { "mul",  M_RRR,  OP_MUL  }, { "and",  M_RRR,  OP_AND  },
    { "or",   M_RRR,  OP_OR   }, { "xor",  M_RRR,  OP_XOR  },
    { "shl",  M_RRR,  OP_SHL  }, { "shr",  M_RRR,  OP_SHR  },
    { "addi", M_ADDI, OP_ADDI }, { "ldw",  M_LDW,  OP_LDW  },
    { "stw",  M_STW,  OP_STW  }, { "bz",   M_BZ,   OP_BZ   },
    { "bnz",  M_BNZ,  OP_BNZ  }, { "jmp",  M_JMP,  OP_JMP  },
    { "call", M_CALL, OP_CALL }, { "ret",  M_RET,  OP_RET  },
    /* Assembler-only pseudo-ops.  Neither changes the architectural state:
     * NOP is documented as "ADDI r0, r0, 0", which the regfile already
     * discards, and MV is "ADDI rd, rs, 0". */
    { "nop",  M_ADDI, OP_ADDI },
    { "mv",   M_ADDI, OP_ADDI },
};

static int lookup_mnemonic(const char *s, size_t len,
                           const struct mnem_entry **e)
{
    size_t i;

    for (i = 0; i < sizeof mnemonics / sizeof mnemonics[0]; i++) {
        if (ci_eqn(s, len, mnemonics[i].name)) {
            *e = &mnemonics[i];
            return 1;
        }
    }
    return 0;
}

/* Assemble one instruction at c->addr.  Emits a word in pass 2 only. */
static void assemble_insn(struct as_ctx *c, const struct mnem_entry *e,
                          const char *mnem, size_t mnem_len)
{
    char name[24];
    size_t n = mnem_len < sizeof name - 1 ? mnem_len : sizeof name - 1;
    uint32_t pc = c->addr;
    uint32_t word = 0;
    int is_nop, is_mv;

    memcpy(name, mnem, n);
    name[n] = '\0';
    is_nop = ci_eqn(name, n, "nop");
    is_mv  = ci_eqn(name, n, "mv");

    /* The compressed encodings are documented in docs/09-compressed.md, but
     * the shipped core has no compressed decode path -- a 0xF-class word
     * would execute as something else entirely.  Refusing is the only honest
     * option.  The lookup below never matches a C.* form, so the diagnostic
     * has to be raised here, in assemble_line. */
    if (n >= 2 && lower((unsigned char)name[0]) == 'c' && name[1] == '.') {
        as_error(c, "'%s' is a compressed encoding. The shipped core has no "
                    "compressed decode path, so this word would execute as a "
                    "different instruction. Write the 32-bit form.", name);
        return;
    }

    switch (e->m) {
    case M_RRR: {
        uint32_t rd, rs1, rs2;
        if (!parse_reg(c, &rd, "rd") || !expect_comma(c) ||
            !parse_reg(c, &rs1, "rs1") || !expect_comma(c) ||
            !parse_reg(c, &rs2, "rs2"))
            return;
        if (!at_eol(c)) {
            as_error(c, "unexpected text after the operands");
            return;
        }
        word = ENC(e->op, rd, rs1, rs2, 0);
        break;
    }

    case M_ADDI: {
        uint32_t rd = 0, rs1 = 0;
        int64_t imm = 0;

        if (is_nop) {
            if (!at_eol(c)) {
                as_error(c, "NOP takes no operands");
                return;
            }
            /* Write to r0, which the regfile hardwires to zero. */
            word = ENC(e->op, 0, 0, 0, 0);
            break;
        }
        if (!parse_reg(c, &rd, "rd") || !expect_comma(c))
            return;
        if (is_mv) {
            if (!parse_reg(c, &rs1, "the source register"))
                return;
            if (!at_eol(c)) {
                as_error(c, "MV takes exactly two operands: MV rd, rs");
                return;
            }
            imm = 0;
        } else {
            if (!parse_reg(c, &rs1, "rs1") || !expect_comma(c))
                return;
            expr_begin(c);
            if (!parse_expr(c, &imm))
                return;
            if (!at_eol(c)) {
                as_error(c, "unexpected text after the immediate");
                return;
            }
        }
        if (!check_imm16(c, imm, "this immediate"))
            return;
        word = ENC(e->op, rd, rs1, 0, IMM16(imm));
        break;
    }

    case M_LDW: {
        uint32_t rd, base;
        int64_t off = 0;
        if (!parse_reg(c, &rd, "rd") || !expect_comma(c))
            return;
        if (!parse_mem(c, &base, &off))
            return;
        if (!at_eol(c)) {
            as_error(c, "unexpected text after the memory operand");
            return;
        }
        if (!check_imm16(c, off, "this address offset"))
            return;
        word = ENC(e->op, rd, base, 0, IMM16(off));
        break;
    }

    case M_STW: {
        uint32_t rsrc, base;
        int64_t off = 0;
        /* The source register goes in RS2 [19:16]; RD is unused.  This is
         * what the RTL decodes, and what the 1.3 spec revision now says. */
        if (!parse_reg(c, &rsrc, "the source register") || !expect_comma(c))
            return;
        if (!parse_mem(c, &base, &off))
            return;
        if (!at_eol(c)) {
            as_error(c, "unexpected text after the memory operand");
            return;
        }
        if (!check_imm16(c, off, "this address offset"))
            return;
        word = ENC(e->op, 0, base, rsrc, IMM16(off));
        break;
    }

    case M_BZ:
    case M_BNZ: {
        uint32_t rs1;
        int64_t off = 0;
        if (!parse_reg(c, &rs1, "the test register") || !expect_comma(c))
            return;
        if (!rel_target(c, &off))
            return;
        if (!at_eol(c)) {
            as_error(c, "unexpected text after the branch target");
            return;
        }
        word = ENC(e->op, 0, rs1, 0, IMM16(off));
        break;
    }

    case M_JMP: {
        int64_t off = 0;
        if (!rel_target(c, &off))
            return;
        if (!at_eol(c)) {
            as_error(c, "unexpected text after the jump target");
            return;
        }
        word = ENC(e->op, 0, 0, 0, IMM16(off));
        break;
    }

    case M_CALL: {
        int64_t off = 0;
        uint32_t link = REG_LR;

        /* The documented form names the link register.  The core writes lr
         * whatever RD holds, so naming a different one is an error rather
         * than a silently-ignored operand. */
        {
            uint32_t r;
            const char *save = c->p;
            if (peek_reg(c, &r)) {
                skip_spaces(c);
                if (*c->p == ',') {
                    link = r;
                    c->p++;
                } else {
                    c->p = save;      /* it was a label, not a register */
                }
            }
        }
        if (link != REG_LR) {
            as_error(c, "CALL always writes the link register r14 (lr); "
                        "r%u is not a link register on this ISA", link);
            return;
        }
        if (!rel_target(c, &off))
            return;
        if (!at_eol(c)) {
            as_error(c, "unexpected text after the call target");
            return;
        }
        word = ENC(e->op, 0, 0, 0, IMM16(off));
        break;
    }

    case M_RET: {
        uint32_t rs1 = REG_LR;
        /* RET is PC <- rs1, not a fixed word.  Bare RET returns through lr. */
        if (!at_eol(c)) {
            if (!parse_reg(c, &rs1, "the return register"))
                return;
            if (!at_eol(c)) {
                as_error(c, "unexpected text after the RET operand");
                return;
            }
        }
        word = ENC(e->op, 0, rs1, 0, 0);
        break;
    }

    default:
        as_error(c, "internal error: unhandled mnemonic '%s'", name);
        return;
    }

    if (c->pass == 2) {
        emit_word(c, pc, word);
        listing_push(c, pc, word, 1);
    }
}

/* ─── Directives ─────────────────────────────────────────────────────── */

static int want_imm(struct as_ctx *c, int64_t *v, const char *who)
{
    expr_begin(c);
    if (!parse_expr(c, v))
        return 0;
    if (c->unresolved) {
        as_error(c, "%s needs a resolved value, not a forward reference", who);
        return 0;
    }
    return 1;
}

static void do_equ(struct as_ctx *c)
{
    const char *start;
    size_t len;
    int64_t v = 0;

    skip_spaces(c);
    start = c->p;
    while (is_alnum((unsigned char)*c->p))
        c->p++;
    len = (size_t)(c->p - start);
    if (len == 0) {
        as_error(c, ".equ needs a name");
        return;
    }
    if (is_digit((unsigned char)start[0])) {
        as_error(c, "a symbol cannot start with a digit");
        return;
    }
    if (!expect_comma(c))
        return;
    if (!want_imm(c, &v, ".equ"))
        return;
    sym_define(c, start, len, (uint32_t)(int32_t)v);
}

static void directive(struct as_ctx *c, const char *name, size_t nlen)
{
    int64_t v = 0;

    if (ci_eqn(name, nlen, "org")) {
        if (!want_imm(c, &v, ".org"))
            return;
        if (v < 0 || v > 0xFFFFFFFFll) {
            as_error(c, ".org address %lld is out of range", (long long)v);
            return;
        }
        if ((v & 3ll) != 0) {
            /* The core indexes instruction memory by pc[9:2], so an
             * instruction at a non-multiple-of-four address does not exist. */
            as_error(c, ".org address 0x%llx is not word-aligned; BradISA "
                        "instructions are 4 bytes", (unsigned long long)v);
            return;
        }
        c->addr = (uint32_t)v;
        return;
    }

    /* .word / .half / .byte all take a comma-separated list. */
    if (ci_eqn(name, nlen, "word") || ci_eqn(name, nlen, "long") ||
        ci_eqn(name, nlen, "dword")) {
        for (;;) {
            if (!want_imm(c, &v, ".word"))
                return;
            if (c->pass == 2)
                emit_word(c, c->addr, (uint32_t)(int32_t)v);
            c->addr += 4;
            skip_spaces(c);
            if (*c->p != ',')
                break;
            c->p++;
        }
        return;
    }

    if (ci_eqn(name, nlen, "half") || ci_eqn(name, nlen, "short")) {
        for (;;) {
            if (!want_imm(c, &v, ".half"))
                return;
            if (v < -32768 || v > 65535) {
                as_error(c, ".half value %lld does not fit in 16 bits",
                         (long long)v);
                return;
            }
            if (c->pass == 2) {
                emit_byte(c, c->addr,     (uint8_t)(v & 0xFF));
                emit_byte(c, c->addr + 1, (uint8_t)((v >> 8) & 0xFF));
            }
            c->addr += 2;
            skip_spaces(c);
            if (*c->p != ',')
                break;
            c->p++;
        }
        return;
    }

    if (ci_eqn(name, nlen, "byte")) {
        for (;;) {
            if (!want_imm(c, &v, ".byte"))
                return;
            if (v < -128 || v > 255) {
                as_error(c, ".byte value %lld does not fit in 8 bits",
                         (long long)v);
                return;
            }
            if (c->pass == 2)
                emit_byte(c, c->addr, (uint8_t)(v & 0xFF));
            c->addr += 1;
            skip_spaces(c);
            if (*c->p != ',')
                break;
            c->p++;
        }
        return;
    }

    if (ci_eqn(name, nlen, "entry")) {
        if (!want_imm(c, &v, ".entry"))
            return;
        if (c->pass == 2) {
            c->out->entry_point = (uint32_t)v;
            c->out->have_entry = 1;
        }
        return;
    }

    if (ci_eqn(name, nlen, "equ") || ci_eqn(name, nlen, "set")) {
        do_equ(c);
        return;
    }

    if (ci_eqn(name, nlen, "fill")) {
        int64_t count = 0, val = 0, i;
        if (!want_imm(c, &count, ".fill"))
            return;
        if (count < 0 || count > (int64_t)BRAD_AS_MAX_WORDS) {
            as_error(c, ".fill count %lld is out of range (0..%u)",
                     (long long)count, (unsigned)BRAD_AS_MAX_WORDS);
            return;
        }
        skip_spaces(c);
        if (*c->p == ',') {
            c->p++;
            if (!want_imm(c, &val, ".fill"))
                return;
        }
        for (i = 0; i < count; i++) {
            if (c->pass == 2)
                emit_word(c, c->addr, (uint32_t)(int32_t)val);
            c->addr += 4;
        }
        return;
    }

    if (ci_eqn(name, nlen, "align")) {
        int64_t a = 4;
        skip_spaces(c);
        if (*c->p != '\0' && *c->p != '\n' && *c->p != ';' && *c->p != ',') {
            if (!want_imm(c, &a, ".align"))
                return;
        }
        if (a <= 0 || (a & (a - 1)) != 0) {
            as_error(c, ".align needs a power of two, got %lld", (long long)a);
            return;
        }
        while ((c->addr & (uint32_t)(a - 1)) != 0) {
            if (c->pass == 2)
                emit_word(c, c->addr, 0);
            c->addr += 4;
        }
        return;
    }

    /* Accepted and ignored, so source written for a core that does emit
     * compressed forms still assembles.  bradasm never compresses: the
     * shipped core cannot decode it. */
    if (ci_eqn(name, nlen, "compress") || ci_eqn(name, nlen, "nocompress"))
        return;

    if (ci_eqn(name, nlen, "text") || ci_eqn(name, nlen, "data") ||
        ci_eqn(name, nlen, "section") || ci_eqn(name, nlen, "globl") ||
        ci_eqn(name, nlen, "global") || ci_eqn(name, nlen, "type") ||
        ci_eqn(name, nlen, "size") || ci_eqn(name, nlen, "p2align"))
        return;

    {
        char tmp[24];
        size_t k = nlen < sizeof tmp - 1 ? nlen : sizeof tmp - 1;
        memcpy(tmp, name, k);
        tmp[k] = '\0';
        as_error(c, "unknown directive '.%s'", tmp);
    }
}

/* ─── Line and file driver ───────────────────────────────────────────── */

static void assemble_line(struct as_ctx *c)
{
    const char *start;
    size_t len;

    skip_spaces(c);
    /* Consume the comment, or the driver below spins forever: it only
     * advances the cursor on '\n'. */
    if (*c->p == ';') {
        while (*c->p && *c->p != '\n')
            c->p++;
        return;
    }
    if (*c->p == '\0' || *c->p == '\n')
        return;

    /* labels, possibly several on one line */
    for (;;) {
        start = c->p;
        while (is_alnum((unsigned char)*c->p))
            c->p++;
        len = (size_t)(c->p - start);
        if (len > 0 && *c->p == ':') {
            if (is_digit((unsigned char)start[0])) {
                as_error(c, "a label cannot start with a digit");
                return;
            }
            sym_define(c, start, len, c->addr);
            if (c->pass == 2)
                listing_push(c, c->addr, 0, 0);
            c->p++;
            skip_spaces(c);
            if (*c->p == '\0' || *c->p == '\n' || *c->p == ';')
                return;
            continue;
        }
        c->p = start;
        break;
    }

    if (*c->p == '\0' || *c->p == '\n')
        return;

    if (*c->p == '.') {
        start = ++c->p;
        while (is_alpha((unsigned char)*c->p))
            c->p++;
        len = (size_t)(c->p - start);
        if (len == 0) {
            as_error(c, "expected a directive name after '.'");
            return;
        }
        directive(c, start, len);
        return;
    }

    start = c->p;
    while (is_alnum((unsigned char)*c->p))
        c->p++;
    len = (size_t)(c->p - start);
    if (len == 0) {
        as_error(c, "expected a mnemonic");
        return;
    }
    {
        const struct mnem_entry *e;
        if (!lookup_mnemonic(start, len, &e)) {
            char tmp[24];
            size_t k = len < sizeof tmp - 1 ? len : sizeof tmp - 1;
            memcpy(tmp, start, k);
            tmp[k] = '\0';
            if (len >= 2 && lower((unsigned char)tmp[0]) == 'c' && tmp[1] == '.') {
                /* The compressed encodings are documented in
                 * docs/09-compressed.md, but the shipped core has no
                 * compressed decode path, so a 0xF-class word would execute
                 * as something else entirely.  Saying "unknown instruction"
                 * would be true and useless. */
                as_error(c, "'%s' is a compressed encoding. The shipped core "
                            "has no compressed decode path, so this word would "
                            "execute as a different instruction. Write the "
                            "32-bit form.", tmp);
            } else {
                as_error(c, "unknown instruction '%s'", tmp);
            }
            return;
        }
        assemble_insn(c, e, start, len);
        c->addr += 4;
    }
}

static void assemble_source(struct as_ctx *c, const char *src)
{
    c->p = src;
    c->line = 1;
    c->addr = 0;

    while (*c->p) {
        const char *before;

        if (*c->p == '\n') {
            c->p++;
            c->line++;
            continue;
        }
        before = c->p;
        assemble_line(c);
        if (c->p == before) {
            /* assemble_line is not meant to leave the cursor where it found
             * it, but an assembler that hangs is worse than one that
             * complains. Force progress rather than trust that. */
            c->p++;
            c->line++;
        }
    }
}

/* ─── Image finalisation ─────────────────────────────────────────────── */

/* The image was written as bytes starting at address base_addr, but it lives
 * in the same array that must end up holding words.  Slide the byte window
 * down to the start, then pack little-endian in place: each word is read
 * fully into a local before it is written back over the same four bytes. */
static void pack_image(struct as_ctx *c)
{
    uint8_t *img = (uint8_t *)c->out->code;
    uint32_t lo, hi;
    size_t nwords, i;

    if (c->out->limit_addr <= c->out->base_addr) {
        c->out->base_addr = 0;
        c->out->code_words = 0;
        return;
    }

    lo = c->out->base_addr & ~3u;
    hi = (c->out->limit_addr + 3u) & ~3u;
    nwords = (size_t)(hi - lo) / 4u;

    if (lo != 0)
        memmove(img, img + lo, (size_t)(hi - lo));

    for (i = 0; i < nwords; i++) {
        uint32_t w = (uint32_t)img[4 * i]            |
                     ((uint32_t)img[4 * i + 1] << 8)  |
                     ((uint32_t)img[4 * i + 2] << 16) |
                     ((uint32_t)img[4 * i + 3] << 24);
        c->out->code[i] = w;
    }

    c->out->base_addr = lo;
    c->out->code_words = nwords;
}

/* ─── Public entry point ─────────────────────────────────────────────── */

/* The real worker.  `out` may be NULL, in which case the image and symbol
 * table are built in a scratch buffer and thrown away -- which still costs a
 * 256 KiB memset, so a caller that only wants a syntax check should say so
 * and stop asking. */
static size_t assemble_into(const char *source, struct brad_as_output *out,
                            struct brad_as_listing *listing, size_t cap,
                            int *status)
{
    struct as_ctx c;
    struct brad_as_output *o = out;
    size_t rows;

    if (o == NULL) {
        o = (struct brad_as_output *)calloc(1, sizeof *o);
        if (o == NULL) {
            if (status != NULL)
                *status = 1;
            return 0;
        }
    } else {
        memset(o->code, 0, sizeof o->code);
        o->code_words = 0;
        o->base_addr = 0xFFFFFFFFu;     /* lowered by the first emit */
        o->limit_addr = 0;
        o->entry_point = 0;            /* BRAD_EXC_RESET */
        o->have_entry = 0;
        o->symbol_count = 0;
        memset(o->symbols, 0, sizeof o->symbols);
        o->err.ok = 1;
        o->err.line = 0;
        o->err.error[0] = '\0';
    }

    memset(&c, 0, sizeof c);
    c.out = o;
    c.line = 1;
    c.addr = 0;
    c.lst = listing;
    c.lst_cap = listing ? cap : 0;
    c.labels = (struct brad_as_symbol *)calloc(BRAD_AS_MAX_SYMS,
                                               sizeof *c.labels);
    if (c.labels == NULL) {
        o->err.ok = 0;
        o->err.line = 0;
        snprintf(o->err.error, BRAD_AS_ERR_LEN, "out of memory");
        if (status != NULL)
            *status = 1;
        if (out == NULL)
            free(o);
        return 0;
    }

    c.pass = 1;                        /* sizes + label addresses only */
    assemble_source(&c, source ? source : "");

    c.pass = 2;                        /* encode, and diagnose */
    assemble_source(&c, source ? source : "");

    free(c.labels);

    if (o->limit_addr == 0)
        o->base_addr = 0;
    if (o->limit_addr > o->base_addr && o->base_addr == 0xFFFFFFFFu)
        o->base_addr = 0;
    pack_image(&c);

    if (status != NULL)
        *status = o->err.ok ? 0 : 1;
    rows = c.lst_n;
    if (out == NULL)
        free(o);
    return rows;
}

size_t brad_as_assemble_listing(const char *source, struct brad_as_output *out,
                                struct brad_as_listing *listing, size_t cap)
{
    return assemble_into(source, out, listing, cap, NULL);
}

int brad_as_assemble(const char *source, struct brad_as_output *out)
{
    int status = 1;

    assemble_into(source, out, NULL, 0, &status);
    return status;
}

int brad_as_byte(const struct brad_as_output *out, uint32_t addr, uint8_t *byte)
{
    if (out == NULL || out->code_words == 0)
        return 0;
    if (addr < out->base_addr || addr >= out->limit_addr)
        return 0;
    addr -= out->base_addr;
    *byte = (uint8_t)(out->code[addr / 4u] >> ((addr % 4u) * 8u));
    return 1;
}

int brad_as_lookup(const struct brad_as_output *out, const char *name,
                   uint32_t *addr)
{
    size_t i;

    if (out == NULL || name == NULL)
        return 0;
    for (i = 0; i < out->symbol_count; i++) {
        if (strcmp(out->symbols[i].name, name) == 0) {
            *addr = out->symbols[i].addr;
            return 1;
        }
    }
    return 0;
}
