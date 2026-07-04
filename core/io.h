/* Pure I/O interface. The core never calls read()/write()/putchar()
 * directly — only through this struct of function pointers, so the same
 * cpu.c compiles against a simple ASCII backend, an accurate Flexowriter
 * backend, a test-fixture stub, or any other backend a host provides. No
 * implementation lives here; see io/simple.c and io/flexowriter.c. */
#ifndef LGP30_IO_H
#define LGP30_IO_H

struct io_ops {
    /* Reads one character. On success, writes it to *out_char and returns
     * 0. If no character is available yet, returns 1 ("would block") — the
     * step-driven core relies on this to let a print/input instruction
     * span multiple lgp30_step() calls rather than blocking the host. On
     * end-of-input/error, returns -1. */
    int (*read_char)(void *ctx, int *out_char);

    void (*write_char)(void *ctx, int c);
    void (*flush)(void *ctx);

    void *ctx;
};

#endif
