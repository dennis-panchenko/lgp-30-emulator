/* GDB-style interactive debugger. Plain scrolling text, no screen state
 * -- see ABOUT.md: this exists to force the inspection API into
 * shape before other front-ends need the same thing.
 *
 * The simulated machine's `print` output goes to stdout (visible during
 * 'step'/'run', useful for debugging output-producing programs), but its
 * `input` is deliberately NOT wired to stdin: sharing one stream between
 * REPL commands and simulated tape characters would let a mid-word
 * `i 0000` silently consume bytes the user typed as the next command.
 * `i 0000` always reads EOF here and halts immediately instead -- v1 has
 * no interactive tape-input story for the REPL; use the batch CLI
 * (host/cli/main.c) for programs that read input. */
#include "repl.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cpu.h"
#include "display.h"
#include "io.h"
#include "program_loader.h"
#include "types.h"

static int repl_read_char(void *ctx, int *out_char) {
    (void)ctx;
    (void)out_char;
    return -1;
}

static void repl_write_char(void *ctx, int c) {
    FILE *out = ctx;
    fputc(c, out);
}

static void repl_flush(void *ctx) {
    FILE *out = ctx;
    fflush(out);
}

#define MAX_RUN_STEPS 1000000ull

static void print_help(void) {
    printf(
        "Commands:\n"
        "  step                 execute one instruction\n"
        "  run                  execute until a breakpoint, halt, or overflow\n"
        "  break <addr>         set a breakpoint (4-digit decimal address, e.g. 1000)\n"
        "  clear <addr>         clear a breakpoint\n"
        "  registers            show accumulator, counter, instruction register, status\n"
        "  dump <addr> <n>      show n consecutive words starting at addr\n"
        "  reset                clear registers/execution state (keeps memory, breakpoints)\n"
        "  load <file>          assemble and load a program\n"
        "  help                 show this text\n"
        "  quit                 exit\n");
}

static bool parse_addr_arg(const char *text, uint16_t *out) {
    if (text == NULL || strlen(text) != 4) {
        return false;
    }
    return lgp30_parse_address(text, out);
}

static void cmd_step(struct LGP30 *m) {
    enum lgp30_status s = lgp30_step(m);
    char addr[5];
    lgp30_format_address((uint16_t)lgp30_read_register(m, LGP30_REG_COUNTER), addr);
    printf("counter now %s, status: %s\n", addr, status_name(s));
}

static void cmd_run(struct LGP30 *m) {
    for (uint64_t i = 0; i < MAX_RUN_STEPS; i++) {
        uint16_t counter = (uint16_t)lgp30_read_register(m, LGP30_REG_COUNTER);
        if (i > 0 && lgp30_is_breakpoint(m, counter)) {
            char addr[5];
            lgp30_format_address(counter, addr);
            printf("breakpoint hit at %s\n", addr);
            return;
        }
        enum lgp30_status s = lgp30_step(m);
        if (s == LGP30_STOPPED || s == LGP30_OVERFLOW) {
            printf("halted: %s\n", status_name(s));
            return;
        }
    }
    printf("step limit reached (%llu steps)\n", (unsigned long long)MAX_RUN_STEPS);
}

static void cmd_break(struct LGP30 *m, const char *arg) {
    uint16_t addr;
    if (!parse_addr_arg(arg, &addr)) {
        printf("usage: break <4-digit-decimal-address>\n");
        return;
    }
    lgp30_set_breakpoint(m, addr);
    printf("breakpoint set at %s\n", arg);
}

static void cmd_clear(struct LGP30 *m, const char *arg) {
    uint16_t addr;
    if (!parse_addr_arg(arg, &addr)) {
        printf("usage: clear <4-digit-decimal-address>\n");
        return;
    }
    lgp30_clear_breakpoint(m, addr);
    printf("breakpoint cleared at %s\n", arg);
}

static void cmd_registers(struct LGP30 *m) {
    char acc_hex[9], instr_hex[9], counter_addr[5];
    format_word_hex(lgp30_read_register(m, LGP30_REG_ACCUMULATOR), acc_hex);
    format_word_hex(lgp30_read_register(m, LGP30_REG_INSTRUCTION_REGISTER), instr_hex);
    lgp30_format_address((uint16_t)lgp30_read_register(m, LGP30_REG_COUNTER), counter_addr);
    printf("accumulator:          %s\n", acc_hex);
    printf("counter:              %s\n", counter_addr);
    printf("instruction register: %s\n", instr_hex);
    printf("status:               %s\n", status_name(lgp30_get_status(m)));
}

static void cmd_dump(struct LGP30 *m, const char *addr_arg, const char *n_arg) {
    uint16_t addr;
    if (!parse_addr_arg(addr_arg, &addr) || n_arg == NULL) {
        printf("usage: dump <4-digit-decimal-address> <count>\n");
        return;
    }
    char *end;
    long count = strtol(n_arg, &end, 10);
    if (end == n_arg || count <= 0) {
        printf("usage: dump <4-digit-decimal-address> <count>\n");
        return;
    }
    for (long i = 0; i < count; i++) {
        uint16_t a = (uint16_t)((addr + i) & 0xFFF);
        char a_text[5], word_hex[9];
        lgp30_format_address(a, a_text);
        format_word_hex(lgp30_read_memory(m, a), word_hex);
        printf("%s: %s\n", a_text, word_hex);
    }
}

int repl_main(int argc, char **argv) {
    struct LGP30 *m = lgp30_create();
    struct io_ops io;
    io.read_char = repl_read_char;
    io.write_char = repl_write_char;
    io.flush = repl_flush;
    io.ctx = stdout;
    lgp30_attach_io(m, &io);

    if (argc > 2) {
        if (load_program(m, argv[2]) == LOAD_OK) {
            printf("loaded %s\n", argv[2]);
        }
    }

    print_help();
    printf("\n");

    char line[256];
    for (;;) {
        printf("lgp30> ");
        fflush(stdout);
        if (fgets(line, sizeof(line), stdin) == NULL) {
            printf("\n");
            break;
        }
        char *newline = strchr(line, '\n');
        if (newline != NULL) {
            *newline = '\0';
        }

        char *cmd = strtok(line, " \t");
        if (cmd == NULL) {
            continue;
        }
        char *arg1 = strtok(NULL, " \t");
        char *arg2 = strtok(NULL, " \t");

        if (strcmp(cmd, "step") == 0) {
            cmd_step(m);
        } else if (strcmp(cmd, "run") == 0) {
            cmd_run(m);
        } else if (strcmp(cmd, "break") == 0) {
            cmd_break(m, arg1);
        } else if (strcmp(cmd, "clear") == 0) {
            cmd_clear(m, arg1);
        } else if (strcmp(cmd, "registers") == 0) {
            cmd_registers(m);
        } else if (strcmp(cmd, "dump") == 0) {
            cmd_dump(m, arg1, arg2);
        } else if (strcmp(cmd, "reset") == 0) {
            lgp30_reset(m);
            printf("reset\n");
        } else if (strcmp(cmd, "load") == 0) {
            if (arg1 == NULL) {
                printf("usage: load <file>\n");
            } else if (load_program(m, arg1) == LOAD_OK) {
                printf("loaded %s\n", arg1);
            }
        } else if (strcmp(cmd, "help") == 0) {
            print_help();
        } else if (strcmp(cmd, "quit") == 0 || strcmp(cmd, "exit") == 0) {
            break;
        } else {
            printf("unknown command '%s' (try 'help')\n", cmd);
        }
    }

    lgp30_destroy(m);
    return 0;
}
