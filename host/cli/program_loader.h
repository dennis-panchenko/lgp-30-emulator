/* Shared by main.c (batch) and repl.c (interactive): read a source file,
 * assemble it, and load the result into a machine's memory. */
#ifndef LGP30_HOST_PROGRAM_LOADER_H
#define LGP30_HOST_PROGRAM_LOADER_H

#include "cpu.h"

/* Returns NULL on a read error (out_error, if non-NULL, gets a short
 * static description); caller owns the returned buffer (free()). */
char *read_entire_file(const char *path, const char **out_error);

enum load_result {
    LOAD_OK,
    LOAD_FILE_ERROR, /* couldn't read the file; one line printed to stderr */
    LOAD_ASM_ERROR,  /* assembly failed; one "<path>:<line>:<col>: <msg>" line
                       * per error printed to stderr */
};

/* Reads, assembles, and (on success) copies the resulting image into
 * machine's memory, setting its counter to the image's start address if
 * one was declared. Leaves machine unmodified on any failure. */
enum load_result load_program(struct LGP30 *machine, const char *path);

#endif
