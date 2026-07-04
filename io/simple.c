#include "simple.h"

static int simple_read_char(void *ctx, int *out_char) {
    struct simple_io_ctx *s = ctx;
    int c = fgetc(s->in);
    if (c == EOF) {
        return -1;
    }
    *out_char = c;
    return 0;
}

static void simple_write_char(void *ctx, int c) {
    struct simple_io_ctx *s = ctx;
    fputc(c, s->out);
}

static void simple_flush(void *ctx) {
    struct simple_io_ctx *s = ctx;
    fflush(s->out);
}

struct io_ops simple_io_ops(struct simple_io_ctx *ctx) {
    struct io_ops io;
    io.read_char = simple_read_char;
    io.write_char = simple_write_char;
    io.flush = simple_flush;
    io.ctx = ctx;
    return io;
}
