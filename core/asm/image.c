#include "image.h"

#include <string.h>

void image_init(struct asm_image *img) {
    for (size_t i = 0; i < LGP30_MEMORY_WORDS; i++) {
        img->memory[i] = 0;
        img->assigned[i] = false;
    }
    img->start_address = 0;
    img->has_start_address = false;
}

bool image_write(const struct asm_image *img, FILE *out) {
    if (img->has_start_address) {
        if (fprintf(out, "start %u\n", (unsigned)img->start_address) < 0) {
            return false;
        }
    }
    for (size_t addr = 0; addr < LGP30_MEMORY_WORDS; addr++) {
        if (!img->assigned[addr]) {
            continue;
        }
        if (fprintf(out, "%u %u\n", (unsigned)addr, (unsigned)img->memory[addr]) < 0) {
            return false;
        }
    }
    return true;
}

bool image_read(struct asm_image *img, FILE *in) {
    image_init(img);
    char line[128];
    while (fgets(line, sizeof(line), in) != NULL) {
        if (line[0] == '\n' || line[0] == '\0') {
            continue;
        }
        char keyword[8];
        unsigned start_addr;
        if (sscanf(line, "%7s %u", keyword, &start_addr) == 2 &&
            strcmp(keyword, "start") == 0) {
            img->start_address = (uint16_t)start_addr;
            img->has_start_address = true;
            continue;
        }
        unsigned addr, value;
        if (sscanf(line, "%u %u", &addr, &value) != 2) {
            return false;
        }
        if (addr >= LGP30_MEMORY_WORDS) {
            return false;
        }
        img->memory[addr] = (word32)value;
        img->assigned[addr] = true;
    }
    return true;
}
