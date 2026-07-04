/* Accurate I/O backend: the real Flexowriter 6-bit keyboard code table
 * (manual p.33), with Lower Case / Upper Case shift-state tracking on
 * output. Code *values* are high-confidence (independently cross-checked
 * via the "+4 per opcode/digit" arithmetic pattern visible in the table,
 * and the manual's own pencil-annotated decimal conversions); three
 * Numerical-block upper-case symbol glyphs (codes 18, 30, 34) are OCR-
 * uncertain and approximated -- see FLEXO_TABLE below.
 *
 * Input reads raw pre-encoded code bytes (0-63) from the input stream,
 * not human-typed ASCII -- reversing the shift-state-dependent output
 * table unambiguously would require the input tape to carry explicit
 * shift codes, which is a real paper-tape format v1 does not implement.
 * Use io/simple.c for ASCII-text input, or feed pre-encoded tapes here. */
#ifndef LGP30_IO_FLEXOWRITER_H
#define LGP30_IO_FLEXOWRITER_H

#include <stdbool.h>
#include <stdio.h>

#include "io.h"

struct flexo_io_ctx {
    FILE *in;
    FILE *out;
    bool upper_case; /* current output shift state; starts false (lower) */
};

struct io_ops flexo_io_ops(struct flexo_io_ctx *ctx);

#endif
