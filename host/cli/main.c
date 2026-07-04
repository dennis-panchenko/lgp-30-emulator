/* Batch CLI: assemble a program, run it non-interactively, report how it
 * halted. No inspection, no interactivity -- see host/cli/repl.c
 * for that. This is the first place <stdio.h> appears, per ABOUT.md's
 * "no platform headers in core" rule. */
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cpu.h"
#include "display.h"
#include "flexowriter.h"
#include "io.h"
#include "program_loader.h"
#include "repl.h"
#include "simple.h"
#include "types.h"

enum exit_code {
    EXIT_OK = 0,
    EXIT_ASM_ERROR = 1,
    EXIT_FILE_ERROR = 2,
    EXIT_OVERFLOW = 3,
    EXIT_MAX_STEPS = 4,
};

static void print_usage(FILE *out, const char *prog) {
    fprintf(out,
            "Usage: %s [options] <program.asm>\n"
            "       %s repl [program.asm]\n"
            "\n"
            "Options:\n"
            "  --io=simple|flexowriter  I/O backend (default: simple)\n"
            "  --max-steps=N            safety cap on executed steps (default: 1000000)\n"
            "  --verbose                print cycle count, halt reason, and a register dump\n"
            "  -h, --help               show this help\n",
            prog, prog);
}

int main(int argc, char **argv) {
    if (argc > 1 && strcmp(argv[1], "repl") == 0) {
        return repl_main(argc, argv);
    }

    const char *program_path = NULL;
    const char *io_mode = "simple";
    uint64_t max_steps = 1000000;
    bool verbose = false;

    for (int i = 1; i < argc; i++) {
        const char *arg = argv[i];
        if (strcmp(arg, "-h") == 0 || strcmp(arg, "--help") == 0) {
            print_usage(stdout, argv[0]);
            return EXIT_OK;
        } else if (strncmp(arg, "--io=", 5) == 0) {
            io_mode = arg + 5;
        } else if (strncmp(arg, "--max-steps=", 12) == 0) {
            max_steps = strtoull(arg + 12, NULL, 10);
        } else if (strcmp(arg, "--verbose") == 0) {
            verbose = true;
        } else if (arg[0] == '-') {
            fprintf(stderr, "%s: unknown option '%s'\n", argv[0], arg);
            print_usage(stderr, argv[0]);
            return EXIT_ASM_ERROR;
        } else if (program_path == NULL) {
            program_path = arg;
        } else {
            fprintf(stderr, "%s: unexpected extra argument '%s'\n", argv[0], arg);
            return EXIT_ASM_ERROR;
        }
    }

    if (program_path == NULL) {
        print_usage(stderr, argv[0]);
        return EXIT_ASM_ERROR;
    }
    if (strcmp(io_mode, "simple") != 0 && strcmp(io_mode, "flexowriter") != 0) {
        fprintf(stderr, "%s: --io must be 'simple' or 'flexowriter', got '%s'\n", argv[0], io_mode);
        return EXIT_ASM_ERROR;
    }

    struct LGP30 *machine = lgp30_create();
    enum load_result load_status = load_program(machine, program_path);
    if (load_status != LOAD_OK) {
        lgp30_destroy(machine);
        return load_status == LOAD_FILE_ERROR ? EXIT_FILE_ERROR : EXIT_ASM_ERROR;
    }

    struct simple_io_ctx simple_ctx;
    struct flexo_io_ctx flexo_ctx;
    struct io_ops io;
    if (strcmp(io_mode, "flexowriter") == 0) {
        flexo_ctx.in = stdin;
        flexo_ctx.out = stdout;
        flexo_ctx.upper_case = false;
        io = flexo_io_ops(&flexo_ctx);
    } else {
        simple_ctx.in = stdin;
        simple_ctx.out = stdout;
        io = simple_io_ops(&simple_ctx);
    }
    lgp30_attach_io(machine, &io);

    uint64_t steps = 0;
    enum lgp30_status status = LGP30_RUNNING;
    for (;;) {
        status = lgp30_step(machine);
        steps++;
        if (status == LGP30_STOPPED || status == LGP30_OVERFLOW) {
            break;
        }
        if (steps >= max_steps) {
            break;
        }
    }
    io.flush(io.ctx);

    bool max_steps_hit = (status != LGP30_STOPPED && status != LGP30_OVERFLOW);
    int exit_code = EXIT_OK;
    if (status == LGP30_OVERFLOW) {
        exit_code = EXIT_OVERFLOW;
    } else if (max_steps_hit) {
        exit_code = EXIT_MAX_STEPS;
    }

    if (verbose) {
        char acc_hex[9];
        format_word_hex(machine->accumulator, acc_hex);
        char counter_addr[5];
        lgp30_format_address(machine->counter, counter_addr);
        const char *halt_reason = max_steps_hit ? "max steps exceeded" : status_name(status);
        fprintf(stderr, "steps: %llu\n", (unsigned long long)steps);
        fprintf(stderr, "drum time: %.3f ms\n", (double)machine->drum_ticks / 1e6);
        fprintf(stderr, "halt reason: %s\n", halt_reason);
        fprintf(stderr, "accumulator: %s\n", acc_hex);
        fprintf(stderr, "counter: %s\n", counter_addr);
    }

    lgp30_destroy(machine);
    return exit_code;
}
