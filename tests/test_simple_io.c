#include "test_util.h"
#include "../io/simple.h"

TEST(write_char_passes_bytes_through) {
    FILE *out = tmpfile();
    ASSERT_TRUE(out != NULL, "tmpfile created");
    struct simple_io_ctx ctx = {.in = NULL, .out = out};
    struct io_ops io = simple_io_ops(&ctx);

    io.write_char(io.ctx, 'A');
    io.write_char(io.ctx, 20); /* a raw print track-bits code, not ASCII */
    io.flush(io.ctx);

    rewind(out);
    ASSERT_EQ_INT(fgetc(out), 'A', "first byte passed through unchanged");
    ASSERT_EQ_INT(fgetc(out), 20, "second byte passed through unchanged");
    fclose(out);
}

TEST(read_char_passes_bytes_through) {
    FILE *in = tmpfile();
    ASSERT_TRUE(in != NULL, "tmpfile created");
    fputc('Z', in);
    fputc(5, in);
    rewind(in);

    struct simple_io_ctx ctx = {.in = in, .out = NULL};
    struct io_ops io = simple_io_ops(&ctx);

    int c = -1;
    ASSERT_EQ_INT(io.read_char(io.ctx, &c), 0, "first read succeeds");
    ASSERT_EQ_INT(c, 'Z', "first byte read back unchanged");
    ASSERT_EQ_INT(io.read_char(io.ctx, &c), 0, "second read succeeds");
    ASSERT_EQ_INT(c, 5, "second byte read back unchanged");
    ASSERT_EQ_INT(io.read_char(io.ctx, &c), -1, "EOF reported as -1");
    fclose(in);
}

int main(void) {
    RUN_TEST(write_char_passes_bytes_through);
    RUN_TEST(read_char_passes_bytes_through);
    return test_util_report();
}
