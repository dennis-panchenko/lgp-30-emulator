/* Simplified I/O backend: raw byte passthrough over two FILE streams. No
 * Flexowriter code-table translation -- the byte cpu.c hands to
 * write_char (print's raw 6-bit track-bits code) is written as-is, and
 * whatever byte read_char reads is shifted into the accumulator as-is.
 * Good for testing and for programs that don't care about hardware
 * fidelity; see io/flexowriter.c for the accurate mode. */
#ifndef LGP30_IO_SIMPLE_H
#define LGP30_IO_SIMPLE_H

#include <stdio.h>

#include "io.h"

struct simple_io_ctx {
    FILE *in;
    FILE *out;
};

struct io_ops simple_io_ops(struct simple_io_ctx *ctx);

#endif
