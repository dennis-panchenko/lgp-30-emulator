#include "asm.h"

#include <stdarg.h>
#include <stdio.h>  /* vsnprintf only -- pure in-memory formatting, no
                     * actual I/O; not the kind of platform dependency
                     * ABOUT.md's "no platform headers in core" rule is
                     * about. */
#include <stdlib.h>
#include <string.h>

struct token {
    const char *text;
    int len;
    int column; /* 1-indexed */
};

static int tokenize(const char *line, struct token *tokens, int max_tokens) {
    int count = 0;
    int col = 1;
    const char *p = line;
    while (*p != '\0' && count < max_tokens) {
        while (*p == ' ' || *p == '\t') {
            p++;
            col++;
        }
        if (*p == '\0') {
            break;
        }
        const char *start = p;
        int start_col = col;
        while (*p != '\0' && *p != ' ' && *p != '\t') {
            p++;
            col++;
        }
        tokens[count].text = start;
        tokens[count].len = (int)(p - start);
        tokens[count].column = start_col;
        count++;
    }
    return count;
}

static int opcode_value(char c) {
    switch (c) {
        case 'z': return 0x0;
        case 'b': return 0x1;
        case 'y': return 0x2;
        case 'r': return 0x3;
        case 'i': return 0x4;
        case 'd': return 0x5;
        case 'n': return 0x6;
        case 'm': return 0x7;
        case 'p': return 0x8;
        case 'e': return 0x9;
        case 'u': return 0xA;
        case 't': return 0xB;
        case 'h': return 0xC;
        case 'c': return 0xD;
        case 'a': return 0xE;
        case 's': return 0xF;
        default: return -1;
    }
}

static bool token_is(struct token t, const char *literal) {
    size_t n = strlen(literal);
    return (size_t)t.len == n && strncmp(t.text, literal, n) == 0;
}

static bool parse_location_token(struct token t, uint16_t *out) {
    if (t.len != 4) {
        return false;
    }
    return lgp30_parse_address(t.text, out);
}

static bool parse_signed_integer(struct token t, int64_t *out) {
    int i = 0;
    bool neg = false;
    if (t.len == 0) {
        return false;
    }
    if (t.text[0] == '-') {
        neg = true;
        i = 1;
    } else if (t.text[0] == '+') {
        i = 1;
    }
    if (i >= t.len) {
        return false; /* sign with no digits */
    }
    int64_t value = 0;
    for (; i < t.len; i++) {
        if (t.text[i] < '0' || t.text[i] > '9') {
            return false;
        }
        value = value * 10 + (t.text[i] - '0');
        if (value > 0x7FFFFFFFLL) {
            return false; /* generous overflow guard; real range-check follows */
        }
    }
    *out = neg ? -value : value;
    return true;
}

static void add_errorf(struct asm_result *result, int line, int column, const char *fmt, ...) {
    if (result->error_count == result->error_capacity) {
        size_t new_cap = result->error_capacity == 0 ? 8 : result->error_capacity * 2;
        struct asm_error *grown = realloc(result->errors, new_cap * sizeof(*grown));
        if (grown == NULL) {
            return; /* drop the error rather than crash */
        }
        result->errors = grown;
        result->error_capacity = new_cap;
    }
    struct asm_error *e = &result->errors[result->error_count++];
    e->line = line;
    e->column = column;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(e->message, sizeof(e->message), fmt, ap);
    va_end(ap);
}

void asm_assemble(const char *source, struct asm_result *result) {
    image_init(&result->image);
    result->errors = NULL;
    result->error_count = 0;
    result->error_capacity = 0;

    int line_no = 0;
    const char *p = source;
    while (*p != '\0') {
        line_no++;
        const char *line_start = p;
        while (*p != '\0' && *p != '\n') {
            p++;
        }
        size_t line_len = (size_t)(p - line_start);
        if (*p == '\n') {
            p++;
        }

        char linebuf[256];
        if (line_len >= sizeof(linebuf)) {
            add_errorf(result, line_no, 1, "line too long (max %zu characters)", sizeof(linebuf) - 1);
            continue;
        }
        memcpy(linebuf, line_start, line_len);
        linebuf[line_len] = '\0';

        char *comment = strchr(linebuf, ';');
        if (comment != NULL) {
            *comment = '\0';
        }

        struct token tokens[4];
        int n = tokenize(linebuf, tokens, 4);
        if (n == 0) {
            continue; /* blank or comment-only line */
        }
        if (n == 4) {
            add_errorf(result, line_no, tokens[3].column, "too many fields on this line");
            continue;
        }

        if (n == 2 && token_is(tokens[0], "start")) {
            if (result->image.has_start_address) {
                add_errorf(result, line_no, tokens[0].column, "duplicate 'start' directive");
                continue;
            }
            uint16_t addr;
            if (!parse_location_token(tokens[1], &addr)) {
                add_errorf(result, line_no, tokens[1].column,
                           "expected a 4-digit decimal address (track 00-63, sector 00-63)");
                continue;
            }
            result->image.start_address = addr;
            result->image.has_start_address = true;
            continue;
        }

        if (n != 3) {
            add_errorf(result, line_no, 1,
                       "expected '<location> <opcode> <address>', '<location> dw <value>', "
                       "or 'start <address>'");
            continue;
        }

        uint16_t location;
        if (!parse_location_token(tokens[0], &location)) {
            add_errorf(result, line_no, tokens[0].column,
                       "expected a 4-digit decimal address (track 00-63, sector 00-63)");
            continue;
        }
        if (result->image.assigned[location]) {
            add_errorf(result, line_no, tokens[0].column, "location %.4s already assigned", tokens[0].text);
            continue;
        }

        if (token_is(tokens[1], "dw")) {
            int64_t value;
            if (!parse_signed_integer(tokens[2], &value)) {
                add_errorf(result, line_no, tokens[2].column, "expected a signed decimal integer");
                continue;
            }
            if (value > 0x3FFFFFFFLL || value < -0x40000000LL) {
                add_errorf(result, line_no, tokens[2].column,
                           "value out of range (must fit in [-2^30, 2^30-1])");
                continue;
            }
            result->image.memory[location] = lgp30_word_from_signed(value);
            result->image.assigned[location] = true;
            continue;
        }

        if (tokens[1].len != 1) {
            add_errorf(result, line_no, tokens[1].column, "expected a single opcode letter or 'dw'");
            continue;
        }
        int opcode = opcode_value(tokens[1].text[0]);
        if (opcode < 0) {
            add_errorf(result, line_no, tokens[1].column, "unknown opcode letter '%c'", tokens[1].text[0]);
            continue;
        }
        uint16_t operand;
        if (!parse_location_token(tokens[2], &operand)) {
            add_errorf(result, line_no, tokens[2].column,
                       "expected a 4-digit decimal address (track 00-63, sector 00-63)");
            continue;
        }
        result->image.memory[location] = lgp30_make_instruction((uint8_t)opcode, operand);
        result->image.assigned[location] = true;
    }
}

void asm_result_free(struct asm_result *result) {
    free(result->errors);
    result->errors = NULL;
    result->error_count = 0;
    result->error_capacity = 0;
}
