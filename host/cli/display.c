#include "display.h"

void format_word_hex(word32 w, char out[9]) {
    for (int i = 0; i < 8; i++) {
        unsigned nibble = (unsigned)((w >> (28 - i * 4)) & 0xFu);
        out[i] = lgp30_hex_digit_char(nibble);
    }
    out[8] = '\0';
}

const char *status_name(enum lgp30_status s) {
    switch (s) {
        case LGP30_RUNNING: return "running";
        case LGP30_STOPPED: return "stopped (z instruction)";
        case LGP30_OVERFLOW: return "overflow";
        case LGP30_WAITING_FOR_IO: return "waiting for i/o";
    }
    return "unknown";
}
