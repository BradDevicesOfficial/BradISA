/* SPDX-License-Identifier: MIT */
/* bradasm -- the BradISA assembler.
 *
 * brad_as_assemble() turns BradISA assembly source into 32-bit instruction
 * words plus a symbol table and an entry point.  The encodings it emits are
 * the ones the shipped RTL actually decodes: rtl/verilog/bradisa_defines.v
 * and the two brad_core implementations.  Where the prose documentation and
 * the RTL ever disagreed, the RTL won and the prose was corrected -- see the
 * 1.3 entry in spec/bradisa_spec.tex.
 *
 * The output struct is large (it holds a 256 KiB program image), so callers
 * should heap-allocate it.  brad_as_assemble() clears it for you.
 */

#ifndef BRAD_AS_H
#define BRAD_AS_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Program image limits.  65536 words = 256 KiB, which is comfortably larger
 * than the 4 GiB address space divided by the 4-byte instruction stride that
 * any single boot image has ever needed, and small enough to hold on a heap. */
#define BRAD_AS_MAX_WORDS   65536u
#define BRAD_AS_MAX_SYMS    1024u
#define BRAD_AS_SYM_LEN     64
#define BRAD_AS_ERR_LEN     256

struct brad_as_symbol {
    char     name[BRAD_AS_SYM_LEN];
    uint32_t addr;
};

/* Assembler diagnostics live at the end of brad_as_output because that is
 * what callers reach for first.  ok == 0 means read `line` and `error`. */
struct brad_as_error {
    int      ok;            /* 1 = assembled cleanly, 0 = see line/error */
    unsigned line;           /* 1-based source line, 0 when there is no error */
    char     error[BRAD_AS_ERR_LEN];
};

/* One row of an assembly listing: what a single source line produced.
 * This is the whole argument for having an assembler -- a listing is the
 * answer to "what word did that actually become", and it can only be
 * produced by the thing that did the encoding.  Deriving it from the source
 * text afterwards means writing a second, weaker parser that will disagree
 * with the first one, usually about comments. */
struct brad_as_listing {
    unsigned line;           /* 1-based source line this row came from */
    uint32_t addr;           /* byte address; meaningful when has_word */
    uint32_t word;           /* the encoded instruction word */
    int      has_word;       /* 0 for a label or directive: no word emitted */
};

struct brad_as_output {
    /* Emitted image.  code[0] lives at byte address base_addr.  The image
     * spans [base_addr, limit_addr); limit_addr <= base_addr + 4*code_words
     * and is rounded up to a whole number of words, so the array is always
     * safe to read in full.  Gaps left by .org are zero. */
    uint32_t code[BRAD_AS_MAX_WORDS];
    size_t   code_words;
    uint32_t base_addr;
    uint32_t limit_addr;

    /* .entry / the reset vector.  Undefined until a .entry is seen, and
     * defaults to 0 (the documented BRAD_EXC_RESET vector) when it is not. */
    uint32_t entry_point;
    int      have_entry;

    struct brad_as_symbol symbols[BRAD_AS_MAX_SYMS];
    size_t                symbol_count;

    struct brad_as_error err;
};

/* Assemble `source` (a NUL-terminated string) into `out`.
 *
 * Returns 0 on success, non-zero on failure.  On failure `out` still holds
 * whatever was assembled before the error, and `out->err` describes what
 * went wrong and where.  `out` may be NULL only to ask "does this parse?".
 */
int brad_as_assemble(const char *source, struct brad_as_output *out);

/* Assemble, and additionally record a listing: one row per emitted word,
 * plus one row per label definition (has_word == 0, addr set).
 *
 * `listing` may be NULL, in which case this is exactly brad_as_assemble().
 * Writes at most `cap` rows and returns how many were produced; a `cap` of 0
 * counts without writing.  Unlike brad_as_assemble(), the return value is not
 * a success flag -- it is the row count, which is 0 on a source that emitted
 * nothing, so check out->err.ok for status.
 */
size_t brad_as_assemble_listing(const char *source, struct brad_as_output *out,
                                struct brad_as_listing *listing, size_t cap);

/* Re-read the emitted image one byte at a time, little-endian, so callers can
 * feed a .byte/.half data table somewhere that wants bytes rather than words.
 * Returns 0 if the address is outside the emitted image, 1 otherwise. */
int brad_as_byte(const struct brad_as_output *out, uint32_t addr, uint8_t *byte);

/* Look up a symbol.  Returns 1 and fills *addr when found, 0 otherwise. */
int brad_as_lookup(const struct brad_as_output *out, const char *name,
                   uint32_t *addr);

#ifdef __cplusplus
}
#endif

#endif /* BRAD_AS_H */
