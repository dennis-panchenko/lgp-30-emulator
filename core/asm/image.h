/* A memory image: the output of assembling a program, and the format
 * host/cli/main.c loads to run one. Deliberately NOT a simulation of
 * physical paper tape -- just a plain serialization of (address, word)
 * pairs plus an entry point, using decimal integers throughout for
 * simplicity. Historical raw-tape-format compatibility is out of scope
 * for v1. */
#ifndef LGP30_ASM_IMAGE_H
#define LGP30_ASM_IMAGE_H

#include <stdio.h>

#include "../types.h"

struct asm_image {
    word32 memory[LGP30_MEMORY_WORDS];
    bool assigned[LGP30_MEMORY_WORDS]; /* which addresses were actually written */
    uint16_t start_address;
    bool has_start_address;
};

void image_init(struct asm_image *img);

/* Text format: "start <address>" (if has_start_address), then one
 * "<address> <word32-as-decimal>" line per assigned address, in
 * ascending address order. Returns false on a write error. */
bool image_write(const struct asm_image *img, FILE *out);

/* Parses image_write()'s format. Returns false and leaves *img
 * unspecified on a malformed line. */
bool image_read(struct asm_image *img, FILE *in);

#endif
