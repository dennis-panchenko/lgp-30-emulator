/* Shared formatting helpers for main.c and repl.c. */
#ifndef LGP30_HOST_DISPLAY_H
#define LGP30_HOST_DISPLAY_H

#include "types.h"

/* 8 LGP-30 hex digits (0-9 f g j k q w), MSB first -- for raw word
 * content (e.g. a register dump), NOT address notation (which is
 * decimal, see lgp30_format_address in types.h). */
void format_word_hex(word32 w, char out[9]);

const char *status_name(enum lgp30_status s);

#endif
