/* A minimal assembler for the LGP-30. v1 grammar, deliberately small
 * (see ABOUT.md): raw addresses only, no labels/symbols. One
 * instruction/directive per line:
 *
 *   ; a full-line comment
 *   1000 b 2000        ; <location> <opcode-letter> <address>, both in
 *                       ; decimal TTSS notation (see lgp30_parse_address)
 *   1005 dw 5           ; <location> dw <signed-decimal-integer>
 *   start 1000          ; declares the entry point (at most one per program)
 *
 * A trailing "; comment" is allowed on any line. Not LGPSAP syntax --
 * this is our own format, chosen for structured-error-reporting and
 * cross-platform portability reasons (see ABOUT.md). */
#ifndef LGP30_ASM_H
#define LGP30_ASM_H

#include <stddef.h>

#include "image.h"

struct asm_error {
    int line;   /* 1-indexed */
    int column; /* 1-indexed */
    char message[128];
};

struct asm_result {
    struct asm_image image;
    struct asm_error *errors;
    size_t error_count;
    size_t error_capacity; /* internal */
};

/* Assembles NUL-terminated `source`. On success, result->error_count ==
 * 0 and result->image holds the program (a partial image may still be
 * populated even when there are errors, for whatever lines did parse).
 * Caller must call asm_result_free() when done, success or not. */
void asm_assemble(const char *source, struct asm_result *result);
void asm_result_free(struct asm_result *result);

#endif
