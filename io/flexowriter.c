#include "flexowriter.h"

enum flexo_control {
    FLEXO_UNDEFINED = 0, /* default: code not assigned on real hardware */
    FLEXO_PRINTABLE,
    FLEXO_SHIFT_LOWER,
    FLEXO_SHIFT_UPPER,
    FLEXO_COLOR_SHIFT,     /* ribbon color change; no output-side effect here */
    FLEXO_CARRIAGE_RETURN,
    FLEXO_BACK_SPACE,
    FLEXO_TAB,
    FLEXO_COND_STOP,       /* reader/control signal; no printable effect */
    FLEXO_START_READ,      /* reader/control signal; no printable effect */
    FLEXO_DELETE,          /* paper-tape rubout; no printable effect */
};

struct flexo_code {
    enum flexo_control control;
    char lower_glyph; /* meaningful only when control == FLEXO_PRINTABLE */
    char upper_glyph;
};

/* Indexed by the raw 6-bit code (0-63). Values transcribed from the
 * manual's "LGP-30 Input Output / Keyboard Code" table (p.33): Numerical,
 * Commands, Controls, Signs, and Balance of Keyboard blocks. Gaps (13 of
 * the 64 codes) are genuinely unassigned on real hardware, not omissions
 * -- the 51 defined entries below match the manual's own entry count
 * exactly. Codes 18, 30, and 34's upper-case glyphs (originally shown as
 * "∆", an unclear mark before "7", and "Σ") are OCR-uncertain and
 * approximated as '^', '?', '#' respectively; everything else, including
 * every code *value*, is high-confidence (cross-checked against the
 * "code = 2 + 4*digit" / "code = 1 + 4*opcode" arithmetic pattern visible
 * across the Numerical and Commands blocks, and the manual's own
 * handwritten decimal annotations). */
static const struct flexo_code FLEXO_TABLE[64] = {
    [0]  = {FLEXO_START_READ, 0, 0},
    [1]  = {FLEXO_PRINTABLE, 'z', 'Z'},
    [2]  = {FLEXO_PRINTABLE, '0', ')'},
    [3]  = {FLEXO_PRINTABLE, ' ', ' '}, /* Space */
    [4]  = {FLEXO_SHIFT_LOWER, 0, 0},
    [5]  = {FLEXO_PRINTABLE, 'b', 'B'},
    [6]  = {FLEXO_PRINTABLE, '1', 'L'},
    [7]  = {FLEXO_PRINTABLE, '-', '_'}, /* Signs */
    [8]  = {FLEXO_SHIFT_UPPER, 0, 0},
    [9]  = {FLEXO_PRINTABLE, 'y', 'Y'},
    [10] = {FLEXO_PRINTABLE, '2', '*'},
    [11] = {FLEXO_PRINTABLE, '+', '='}, /* Signs */
    [12] = {FLEXO_COLOR_SHIFT, 0, 0},
    [13] = {FLEXO_PRINTABLE, 'r', 'R'},
    [14] = {FLEXO_PRINTABLE, '3', '"'},
    [15] = {FLEXO_PRINTABLE, ';', ':'}, /* Balance of keyboard */
    [16] = {FLEXO_CARRIAGE_RETURN, 0, 0},
    [17] = {FLEXO_PRINTABLE, 'i', 'I'},
    [18] = {FLEXO_PRINTABLE, '4', '^'}, /* upper glyph uncertain, see above */
    [19] = {FLEXO_PRINTABLE, '/', '?'}, /* Balance of keyboard */
    [20] = {FLEXO_BACK_SPACE, 0, 0},
    [21] = {FLEXO_PRINTABLE, 'd', 'D'},
    [22] = {FLEXO_PRINTABLE, '5', '%'},
    [23] = {FLEXO_PRINTABLE, '.', ']'}, /* Balance of keyboard */
    [24] = {FLEXO_TAB, 0, 0},
    [25] = {FLEXO_PRINTABLE, 'n', 'N'},
    [26] = {FLEXO_PRINTABLE, '6', '$'},
    [27] = {FLEXO_PRINTABLE, ',', '['}, /* Balance of keyboard */
    [29] = {FLEXO_PRINTABLE, 'm', 'M'},
    [30] = {FLEXO_PRINTABLE, '7', '?'}, /* upper glyph uncertain, see above */
    [31] = {FLEXO_PRINTABLE, 'v', 'V'}, /* Balance of keyboard */
    [32] = {FLEXO_COND_STOP, 0, 0},
    [33] = {FLEXO_PRINTABLE, 'p', 'P'},
    [34] = {FLEXO_PRINTABLE, '8', '#'}, /* upper glyph uncertain, see above */
    [35] = {FLEXO_PRINTABLE, 'o', 'O'}, /* Balance of keyboard */
    [37] = {FLEXO_PRINTABLE, 'e', 'E'},
    [38] = {FLEXO_PRINTABLE, '9', '('},
    [39] = {FLEXO_PRINTABLE, 'x', 'X'}, /* Balance of keyboard */
    [41] = {FLEXO_PRINTABLE, 'u', 'U'},
    [42] = {FLEXO_PRINTABLE, 'f', 'F'}, /* LGP-30 hex digit 10 */
    [45] = {FLEXO_PRINTABLE, 't', 'T'},
    [46] = {FLEXO_PRINTABLE, 'g', 'G'}, /* LGP-30 hex digit 11 */
    [49] = {FLEXO_PRINTABLE, 'h', 'H'},
    [50] = {FLEXO_PRINTABLE, 'j', 'J'}, /* LGP-30 hex digit 12 */
    [53] = {FLEXO_PRINTABLE, 'c', 'C'},
    [54] = {FLEXO_PRINTABLE, 'k', 'K'}, /* LGP-30 hex digit 13 */
    [57] = {FLEXO_PRINTABLE, 'a', 'A'},
    [58] = {FLEXO_PRINTABLE, 'q', 'Q'}, /* LGP-30 hex digit 14 */
    [61] = {FLEXO_PRINTABLE, 's', 'S'},
    [62] = {FLEXO_PRINTABLE, 'w', 'W'}, /* LGP-30 hex digit 15 */
    [63] = {FLEXO_DELETE, 0, 0},
};

static int flexo_read_char(void *vctx, int *out_char) {
    struct flexo_io_ctx *ctx = vctx;
    int c = fgetc(ctx->in);
    if (c == EOF) {
        return -1;
    }
    *out_char = c;
    return 0;
}

static void flexo_write_char(void *vctx, int code) {
    struct flexo_io_ctx *ctx = vctx;
    if (code < 0 || code > 63) {
        return;
    }
    struct flexo_code entry = FLEXO_TABLE[code];
    switch (entry.control) {
        case FLEXO_SHIFT_LOWER:
            ctx->upper_case = false;
            return;
        case FLEXO_SHIFT_UPPER:
            ctx->upper_case = true;
            return;
        case FLEXO_CARRIAGE_RETURN:
            /* Real hardware distinguishes CR from line feed; mapped to
             * '\n' here for readable text-file output. */
            fputc('\n', ctx->out);
            return;
        case FLEXO_BACK_SPACE:
            fputc('\b', ctx->out);
            return;
        case FLEXO_TAB:
            fputc('\t', ctx->out);
            return;
        case FLEXO_PRINTABLE:
            fputc(ctx->upper_case ? entry.upper_glyph : entry.lower_glyph, ctx->out);
            return;
        case FLEXO_COLOR_SHIFT:
        case FLEXO_COND_STOP:
        case FLEXO_START_READ:
        case FLEXO_DELETE:
        case FLEXO_UNDEFINED:
        default:
            return; /* no visible effect on typewriter output */
    }
}

static void flexo_flush(void *vctx) {
    struct flexo_io_ctx *ctx = vctx;
    fflush(ctx->out);
}

struct io_ops flexo_io_ops(struct flexo_io_ctx *ctx) {
    struct io_ops io;
    io.read_char = flexo_read_char;
    io.write_char = flexo_write_char;
    io.flush = flexo_flush;
    io.ctx = ctx;
    return io;
}
