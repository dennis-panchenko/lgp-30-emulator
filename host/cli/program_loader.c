#include "program_loader.h"

#include <stdio.h>
#include <stdlib.h>

#include "asm/asm.h"

char *read_entire_file(const char *path, const char **out_error) {
    FILE *f = fopen(path, "rb");
    if (f == NULL) {
        if (out_error != NULL) {
            *out_error = "could not open file";
        }
        return NULL;
    }
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        if (out_error != NULL) {
            *out_error = "could not seek file";
        }
        return NULL;
    }
    long size = ftell(f);
    if (size < 0 || fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        if (out_error != NULL) {
            *out_error = "could not determine file size";
        }
        return NULL;
    }
    char *buf = malloc((size_t)size + 1);
    if (buf == NULL) {
        fclose(f);
        if (out_error != NULL) {
            *out_error = "out of memory";
        }
        return NULL;
    }
    size_t read_n = fread(buf, 1, (size_t)size, f);
    fclose(f);
    buf[read_n] = '\0';
    return buf;
}

enum load_result load_program(struct LGP30 *machine, const char *path) {
    const char *read_error = NULL;
    char *source = read_entire_file(path, &read_error);
    if (source == NULL) {
        fprintf(stderr, "%s: %s\n", path, read_error);
        return LOAD_FILE_ERROR;
    }

    struct asm_result result;
    asm_assemble(source, &result);
    free(source);

    if (result.error_count > 0) {
        for (size_t i = 0; i < result.error_count; i++) {
            struct asm_error *e = &result.errors[i];
            fprintf(stderr, "%s:%d:%d: %s\n", path, e->line, e->column, e->message);
        }
        asm_result_free(&result);
        return LOAD_ASM_ERROR;
    }

    for (size_t addr = 0; addr < LGP30_MEMORY_WORDS; addr++) {
        if (result.image.assigned[addr]) {
            machine->memory[addr] = result.image.memory[addr];
        }
    }
    if (result.image.has_start_address) {
        machine->counter = result.image.start_address;
    }
    asm_result_free(&result);
    return LOAD_OK;
}
