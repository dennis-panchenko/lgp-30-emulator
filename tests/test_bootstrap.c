/* The manual's own bootstrap-loader program (p.35), transcribed into our
 * assembly syntax and run end-to-end through the real assembler and CPU --
 * proof the emulator reproduces documented 1957 machine behavior, not just
 * that it compiles.
 *
 * Manual's listing (Location / Instruction / Operand / Notes):
 *   0000  p 0000                       start tape reader
 *   0001  i 0000                       bring in a word
 *   0002  c(2000)        input word
 *   0003  b 0002         c( )
 *   0004  a 0007         1 at 29       modify c( )
 *   0005  y 0002         c( )
 *   0006  u 0000                       return to input
 *   0007  1 at 29
 *
 * "1 at 29" (word value 8: a single 1 bit at bit position 3, the low bit
 * of the sector field) is the augmenter added to location 0002's `c`
 * instruction each pass, incrementing its target address by one sector
 * -- so successive input words land in 2000, 2001, 2002, ... */
#include "test_util.h"
#include "../core/asm/asm.h"
#include "../core/cpu.h"
#include "../core/io.h"
#include "../core/types.h"

struct stub_io_ctx {
    int input[64];
    int input_count;
    int input_pos;
};

static int stub_read_char(void *vctx, int *out_char) {
    struct stub_io_ctx *s = vctx;
    if (s->input_pos >= s->input_count) {
        return -1;
    }
    *out_char = s->input[s->input_pos++];
    return 0;
}

static void stub_write_char(void *vctx, int c) {
    (void)vctx;
    (void)c;
}

static void stub_flush(void *vctx) {
    (void)vctx;
}

static const char *BOOTSTRAP_SOURCE =
    "start 0000\n"
    "0000 p 0000  ; start tape reader\n"
    "0001 i 0000  ; bring in a word\n"
    "0002 c 2000  ; input word\n"
    "0003 b 0002\n"
    "0004 a 0007  ; modify c( )\n"
    "0005 y 0002\n"
    "0006 u 0000  ; return to input\n"
    "0007 dw 4    ; 1 at 29 (bit 3): dw encodes a NUMBER (word = value<<1),\n"
    "             ; so 4 here produces the raw word 8 (bit 3 set), which is\n"
    "             ; what needs to be ADDED to increment the sector field by 1\n";

TEST(bootstrap_program_fills_successive_memory_words) {
    struct asm_result asm_res;
    asm_assemble(BOOTSTRAP_SOURCE, &asm_res);
    ASSERT_EQ_U32(asm_res.error_count, 0, "bootstrap program assembles cleanly");
    ASSERT_TRUE(asm_res.image.has_start_address, "start address present");

    struct stub_io_ctx stub = {0};
    static const int word1[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    static const int word2[8] = {8, 7, 6, 5, 4, 3, 2, 1};
    for (int i = 0; i < 8; i++) {
        stub.input[i] = word1[i];
        stub.input[8 + i] = word2[i];
    }
    stub.input_count = 16;

    struct LGP30 *m = lgp30_create();
    struct io_ops io;
    io.read_char = stub_read_char;
    io.write_char = stub_write_char;
    io.flush = stub_flush;
    io.ctx = &stub;
    lgp30_attach_io(m, &io);

    for (size_t i = 0; i < LGP30_MEMORY_WORDS; i++) {
        if (asm_res.image.assigned[i]) {
            m->memory[i] = asm_res.image.memory[i];
        }
    }
    m->counter = asm_res.image.start_address;

    enum lgp30_status status = LGP30_RUNNING;
    int steps = 0;
    while (status == LGP30_RUNNING && steps < 10000) {
        status = lgp30_step(m);
        steps++;
    }

    /* The third pass finds the tape exhausted (EOF), an emulator-level
     * convenience halt not modeled by the manual (see cpu.c) -- expected
     * here since we only supplied two words. */
    ASSERT_EQ_INT(status, LGP30_STOPPED, "halts cleanly once input is exhausted");

    uint16_t loc2000 = lgp30_address_from_track_sector(20, 0);
    uint16_t loc2001 = lgp30_address_from_track_sector(20, 1);
    ASSERT_EQ_U32(m->memory[loc2000], 0x12345678u, "first word landed at 2000");
    /* 0x87654321, not ...321: eight 4-bit shifts fill all 32 accumulator
     * bits including the spacer (here left at 1), and storing to memory
     * via `c` always clears it -- exactly the manual's own documented
     * behavior (p.34; see lgp30_mask_spacer). */
    ASSERT_EQ_U32(m->memory[loc2001], 0x87654320u, "second word landed at 2001 (self-modified address)");

    uint16_t loc0002 = lgp30_address_from_track_sector(0, 2);
    uint16_t loc2002 = lgp30_address_from_track_sector(20, 2);
    ASSERT_EQ_U32(lgp30_instruction_address(m->memory[loc0002]), loc2002,
                  "the c instruction's own address field advanced to 2002 for a third pass");
    ASSERT_EQ_U32(lgp30_opcode(m->memory[loc0002]), 0xD, "still a 'c' instruction, opcode untouched");

    lgp30_destroy(m);
    asm_result_free(&asm_res);
}

int main(void) {
    RUN_TEST(bootstrap_program_fills_successive_memory_words);
    return test_util_report();
}
