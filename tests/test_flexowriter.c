#include "test_util.h"
#include "../io/flexowriter.h"

static char last_byte(FILE *f) {
    long pos = ftell(f);
    ASSERT_TRUE(pos > 0, "something was written");
    fseek(f, pos - 1, SEEK_SET);
    int c = fgetc(f);
    fseek(f, pos, SEEK_SET);
    return (char)c;
}

TEST(commands_block_default_lower_case) {
    /* Default shift state is lower case; the opcode mnemonic letters
     * round-trip exactly against core's own opcode table. */
    FILE *out = tmpfile();
    struct flexo_io_ctx ctx = {.in = NULL, .out = out, .upper_case = false};
    struct io_ops io = flexo_io_ops(&ctx);

    io.write_char(io.ctx, 1);  /* z, opcode 0 */
    io.write_char(io.ctx, 5);  /* b, opcode 1 */
    io.write_char(io.ctx, 33); /* p, opcode 8 */
    io.write_char(io.ctx, 61); /* s, opcode 15 */
    io.flush(io.ctx);

    rewind(out);
    char buf[8] = {0};
    fread(buf, 1, 4, out);
    ASSERT_TRUE(buf[0] == 'z' && buf[1] == 'b' && buf[2] == 'p' && buf[3] == 's',
                "opcode letters print in lower case by default");
    fclose(out);
}

TEST(shift_state_changes_output) {
    FILE *out = tmpfile();
    struct flexo_io_ctx ctx = {.in = NULL, .out = out, .upper_case = false};
    struct io_ops io = flexo_io_ops(&ctx);

    io.write_char(io.ctx, 1); /* z (lower) */
    io.write_char(io.ctx, 8); /* Upper Case shift control, no output */
    io.write_char(io.ctx, 1); /* Z (upper) */
    io.write_char(io.ctx, 4); /* Lower Case shift control, no output */
    io.write_char(io.ctx, 1); /* z (lower again) */
    io.flush(io.ctx);

    rewind(out);
    char buf[8] = {0};
    size_t n = fread(buf, 1, 4, out);
    ASSERT_EQ_INT((int)n, 3, "shift controls produce no output byte themselves");
    ASSERT_TRUE(buf[0] == 'z' && buf[1] == 'Z' && buf[2] == 'z',
                "shift state toggles the printed case");
    fclose(out);
}

TEST(digit_codes_round_trip) {
    FILE *out = tmpfile();
    struct flexo_io_ctx ctx = {.in = NULL, .out = out, .upper_case = false};
    struct io_ops io = flexo_io_ops(&ctx);

    static const int digit_codes[10] = {2, 6, 10, 14, 18, 22, 26, 30, 34, 38};
    for (int i = 0; i < 10; i++) {
        io.write_char(io.ctx, digit_codes[i]);
    }
    io.flush(io.ctx);

    rewind(out);
    char buf[16] = {0};
    fread(buf, 1, 10, out);
    ASSERT_TRUE(buf[0] == '0' && buf[9] == '9', "digit 0 and 9 land correctly");
    for (int i = 0; i < 10; i++) {
        ASSERT_EQ_INT(buf[i], '0' + i, "digit code prints the matching digit");
    }
    fclose(out);
}

TEST(lgp30_hex_letters_round_trip) {
    /* Codes 42,46,50,54,58,62 are LGP-30 hex digits f g j k q w (10-15). */
    FILE *out = tmpfile();
    struct flexo_io_ctx ctx = {.in = NULL, .out = out, .upper_case = false};
    struct io_ops io = flexo_io_ops(&ctx);

    static const int codes[6] = {42, 46, 50, 54, 58, 62};
    static const char expected[6] = {'f', 'g', 'j', 'k', 'q', 'w'};
    for (int i = 0; i < 6; i++) {
        io.write_char(io.ctx, codes[i]);
    }
    io.flush(io.ctx);

    rewind(out);
    char buf[8] = {0};
    fread(buf, 1, 6, out);
    for (int i = 0; i < 6; i++) {
        ASSERT_EQ_INT(buf[i], expected[i], "matches core's own hex digit alphabet");
    }
    fclose(out);
}

TEST(controls_map_to_expected_bytes) {
    FILE *out = tmpfile();
    struct flexo_io_ctx ctx = {.in = NULL, .out = out, .upper_case = false};
    struct io_ops io = flexo_io_ops(&ctx);

    io.write_char(io.ctx, 3);  /* Space */
    ASSERT_EQ_INT(last_byte(out), ' ', "space");
    io.write_char(io.ctx, 16); /* Car Ret */
    ASSERT_EQ_INT(last_byte(out), '\n', "carriage return");
    io.write_char(io.ctx, 20); /* Back Space */
    ASSERT_EQ_INT(last_byte(out), '\b', "back space");
    io.write_char(io.ctx, 24); /* Tab */
    ASSERT_EQ_INT(last_byte(out), '\t', "tab");
    fclose(out);
}

TEST(undefined_and_delete_codes_produce_no_output) {
    FILE *out = tmpfile();
    struct flexo_io_ctx ctx = {.in = NULL, .out = out, .upper_case = false};
    struct io_ops io = flexo_io_ops(&ctx);

    io.write_char(io.ctx, 63); /* Delete */
    io.write_char(io.ctx, 28); /* unassigned gap */
    io.write_char(io.ctx, 1);  /* z, to prove the stream still works */
    io.flush(io.ctx);

    rewind(out);
    char buf[8] = {0};
    size_t n = fread(buf, 1, 4, out);
    ASSERT_EQ_INT((int)n, 1, "only the printable code produced a byte");
    ASSERT_EQ_INT(buf[0], 'z', "and it's the right one");
    fclose(out);
}

TEST(read_char_passes_raw_codes_through) {
    FILE *in = tmpfile();
    fputc(42, in); /* raw code for 'f'/'F' */
    rewind(in);
    struct flexo_io_ctx ctx = {.in = in, .out = NULL, .upper_case = false};
    struct io_ops io = flexo_io_ops(&ctx);

    int c = -1;
    ASSERT_EQ_INT(io.read_char(io.ctx, &c), 0, "read succeeds");
    ASSERT_EQ_INT(c, 42, "raw code passed through unmapped");
    ASSERT_EQ_INT(io.read_char(io.ctx, &c), -1, "EOF reported as -1");
    fclose(in);
}

int main(void) {
    RUN_TEST(commands_block_default_lower_case);
    RUN_TEST(shift_state_changes_output);
    RUN_TEST(digit_codes_round_trip);
    RUN_TEST(lgp30_hex_letters_round_trip);
    RUN_TEST(controls_map_to_expected_bytes);
    RUN_TEST(undefined_and_delete_codes_produce_no_output);
    RUN_TEST(read_char_passes_raw_codes_through);
    return test_util_report();
}
