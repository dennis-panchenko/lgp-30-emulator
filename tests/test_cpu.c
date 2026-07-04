#include "test_util.h"
#include "../core/cpu.h"
#include "../core/io.h"
#include "../core/types.h"

/* Trivial in-memory io_ops stub: a bounded input queue and output log. */

struct stub_io_ctx {
    int output[64];
    int output_count;
    int input[64];
    int input_count;
    int input_pos;
    bool block_once;
};

static int stub_read_char(void *ctx, int *out_char) {
    struct stub_io_ctx *s = ctx;
    if (s->block_once) {
        s->block_once = false;
        return 1;
    }
    if (s->input_pos >= s->input_count) {
        return -1;
    }
    *out_char = s->input[s->input_pos++];
    return 0;
}

static void stub_write_char(void *ctx, int c) {
    struct stub_io_ctx *s = ctx;
    s->output[s->output_count++] = c;
}

static void stub_flush(void *ctx) {
    (void)ctx;
}

static struct io_ops make_stub_io(struct stub_io_ctx *ctx) {
    struct io_ops io;
    io.read_char = stub_read_char;
    io.write_char = stub_write_char;
    io.flush = stub_flush;
    io.ctx = ctx;
    return io;
}

static struct LGP30 *make_machine(struct stub_io_ctx *stub) {
    struct LGP30 *m = lgp30_create();
    static struct io_ops io;
    io = make_stub_io(stub);
    lgp30_attach_io(m, &io);
    return m;
}

TEST(stop_halts) {
    struct stub_io_ctx stub = {0};
    struct LGP30 *m = make_machine(&stub);
    m->memory[0] = lgp30_make_instruction(0x0, 0);
    enum lgp30_status s = lgp30_step(m);
    ASSERT_EQ_INT(s, LGP30_STOPPED, "z halts");
    ASSERT_EQ_INT(lgp30_step(m), LGP30_STOPPED, "stays halted");
    lgp30_destroy(m);
}

TEST(bring_loads_accumulator) {
    struct stub_io_ctx stub = {0};
    struct LGP30 *m = make_machine(&stub);
    uint16_t data_addr = lgp30_address_from_track_sector(1, 0);
    m->memory[data_addr] = 0x12340000u;
    m->memory[0] = lgp30_make_instruction(0x1, data_addr);
    lgp30_step(m);
    ASSERT_EQ_U32(m->accumulator, 0x12340000u, "b loads mem into acc");
    lgp30_destroy(m);
}

TEST(hold_and_store_keeps_accumulator) {
    struct stub_io_ctx stub = {0};
    struct LGP30 *m = make_machine(&stub);
    uint16_t dst = lgp30_address_from_track_sector(1, 0);
    m->accumulator = 0xAABBCC00u;
    m->memory[0] = lgp30_make_instruction(0xC, dst);
    lgp30_step(m);
    ASSERT_EQ_U32(m->memory[dst], 0xAABBCC00u, "h stores acc");
    ASSERT_EQ_U32(m->accumulator, 0xAABBCC00u, "h leaves acc unchanged");
    lgp30_destroy(m);
}

TEST(clear_and_store_clears_accumulator) {
    struct stub_io_ctx stub = {0};
    struct LGP30 *m = make_machine(&stub);
    uint16_t dst = lgp30_address_from_track_sector(1, 0);
    m->accumulator = 0xAABBCC00u;
    m->memory[0] = lgp30_make_instruction(0xD, dst);
    lgp30_step(m);
    ASSERT_EQ_U32(m->memory[dst], 0xAABBCC00u, "c stores acc");
    ASSERT_EQ_U32(m->accumulator, 0u, "c clears acc");
    lgp30_destroy(m);
}

TEST(store_address_preserves_opcode_bits) {
    struct stub_io_ctx stub = {0};
    struct LGP30 *m = make_machine(&stub);
    uint16_t dst = lgp30_address_from_track_sector(5, 0);
    uint16_t old_addr = lgp30_address_from_track_sector(9, 9);
    uint16_t new_addr = lgp30_address_from_track_sector(10, 20);
    m->memory[dst] = lgp30_make_instruction(0xE, old_addr); /* "a 0909" */
    m->accumulator = lgp30_make_instruction(0x1, new_addr); /* address bits only matter */
    m->memory[0] = lgp30_make_instruction(0x2, dst); /* y dst */
    lgp30_step(m);
    ASSERT_EQ_U32(lgp30_opcode(m->memory[dst]), 0xE, "opcode bits untouched");
    ASSERT_EQ_U32(lgp30_instruction_address(m->memory[dst]), new_addr, "address bits replaced");
    ASSERT_EQ_U32(m->accumulator, lgp30_make_instruction(0x1, new_addr), "acc unaffected");
    lgp30_destroy(m);
}

TEST(return_address_stores_counter_plus_one) {
    /* Mirrors the manual's own example (p.18): "r 3050" located at 1013
     * stores return address 1015 (i.e. counter+1 after the r instruction
     * itself has been fetched) into location 3050, leaving that word's
     * own opcode bits (e.g. a "u 0000") untouched. */
    struct stub_io_ctx stub = {0};
    struct LGP30 *m = make_machine(&stub);
    uint16_t r_loc = lgp30_address_from_track_sector(10, 13);
    uint16_t callee_slot = lgp30_address_from_track_sector(30, 50);
    uint16_t expected_return = lgp30_address_from_track_sector(10, 15);

    m->memory[callee_slot] = lgp30_make_instruction(0xA, 0); /* "u 0000" */
    m->memory[r_loc] = lgp30_make_instruction(0x3, callee_slot); /* "r 3050" */
    m->counter = r_loc;

    lgp30_step(m);

    ASSERT_EQ_U32(lgp30_opcode(m->memory[callee_slot]), 0xA, "opcode preserved");
    ASSERT_EQ_U32(lgp30_instruction_address(m->memory[callee_slot]), expected_return,
                  "address becomes the return point");
    lgp30_destroy(m);
}

TEST(unconditional_transfer_jumps) {
    struct stub_io_ctx stub = {0};
    struct LGP30 *m = make_machine(&stub);
    uint16_t target = lgp30_address_from_track_sector(3, 3);
    m->accumulator = 0xDEADBEEFu;
    m->memory[0] = lgp30_make_instruction(0xA, target);
    lgp30_step(m);
    ASSERT_EQ_U32(m->counter, target, "u jumps");
    ASSERT_EQ_U32(m->accumulator, 0xDEADBEEFu, "u does not touch acc");
    lgp30_destroy(m);
}

TEST(test_branches_when_negative) {
    struct stub_io_ctx stub = {0};
    struct LGP30 *m = make_machine(&stub);
    uint16_t target = lgp30_address_from_track_sector(3, 3);
    m->accumulator = 0x80000000u; /* sign bit set */
    m->memory[0] = lgp30_make_instruction(0xB, target);
    lgp30_step(m);
    ASSERT_EQ_U32(m->counter, target, "t branches on negative");
    lgp30_destroy(m);
}

TEST(test_falls_through_when_nonnegative) {
    struct stub_io_ctx stub = {0};
    struct LGP30 *m = make_machine(&stub);
    uint16_t target = lgp30_address_from_track_sector(3, 3);
    m->accumulator = 0x00000000u;
    m->memory[0] = lgp30_make_instruction(0xB, target);
    lgp30_step(m);
    ASSERT_EQ_U32(m->counter, 1, "t falls through on zero/positive");
    lgp30_destroy(m);
}

TEST(extract_bitwise_and) {
    struct stub_io_ctx stub = {0};
    struct LGP30 *m = make_machine(&stub);
    uint16_t mask_addr = lgp30_address_from_track_sector(1, 0);
    m->memory[mask_addr] = 0xF0F0F0F0u;
    m->accumulator = 0xFFFFFFFFu;
    m->memory[0] = lgp30_make_instruction(0x9, mask_addr);
    lgp30_step(m);
    ASSERT_EQ_U32(m->accumulator, 0xF0F0F0F0u, "extract ANDs with mask");
    ASSERT_EQ_U32(m->memory[mask_addr], 0xF0F0F0F0u, "mask word unaffected");
    lgp30_destroy(m);
}

TEST(add_and_subtract_complement_example) {
    /* Manual's own complement example (p.27): "6 at q=4" is
     * 0|011000...0 = 0x30000000; "-6 at q=4" is 1|101000...0 = 0xD0000000. */
    struct stub_io_ctx stub = {0};
    struct LGP30 *m = make_machine(&stub);
    uint16_t addr = lgp30_address_from_track_sector(1, 0);

    m->memory[addr] = 0xD0000000u; /* -6 */
    m->accumulator = 0x30000000u;  /* +6 */
    m->memory[0] = lgp30_make_instruction(0xE, addr); /* a addr */
    lgp30_step(m);
    ASSERT_EQ_U32(m->accumulator, 0x00000000u, "6 + (-6) = 0");

    lgp30_reset(m);
    m->memory[addr] = 0x30000000u; /* +6 */
    m->accumulator = 0x30000000u;  /* +6 */
    m->memory[0] = lgp30_make_instruction(0xF, addr); /* s addr */
    lgp30_step(m);
    ASSERT_EQ_U32(m->accumulator, 0x00000000u, "6 - 6 = 0");
    lgp30_destroy(m);
}

TEST(add_overflow_halts) {
    struct stub_io_ctx stub = {0};
    struct LGP30 *m = make_machine(&stub);
    uint16_t addr = lgp30_address_from_track_sector(1, 0);
    m->memory[addr] = 0x7FFFFFFEu; /* max positive magnitude */
    m->accumulator = 0x7FFFFFFEu;
    m->memory[0] = lgp30_make_instruction(0xE, addr);
    enum lgp30_status s = lgp30_step(m);
    ASSERT_EQ_INT(s, LGP30_OVERFLOW, "adding two max-positive values overflows");
    lgp30_destroy(m);
}

TEST(m_multiply_manual_example) {
    /* Manual's own worked example (pp.25-26): 3 at q=2 (acc) times 2 at
     * q=2 (mem) = 6 at q=4. Bit patterns transcribed directly from the
     * manual's diagrams. */
    struct stub_io_ctx stub = {0};
    struct LGP30 *m = make_machine(&stub);
    uint16_t addr = lgp30_address_from_track_sector(1, 0);
    m->accumulator = 0x60000000u; /* 3 at q=2 */
    m->memory[addr] = 0x40000000u; /* 2 at q=2 */
    m->memory[0] = lgp30_make_instruction(0x7, addr); /* m addr */
    lgp30_step(m);
    ASSERT_EQ_U32(m->accumulator, 0x30000000u, "3*2 at q=2+2 = 6 at q=4");
    lgp30_destroy(m);
}

TEST(m_multiply_truncation_example) {
    /* Manual's own truncation example (p.26): 3 at q=30 (acc) times 2 at
     * q=2 (mem) should be 6 at q=32 but truncates to 4 at q=32. */
    struct stub_io_ctx stub = {0};
    struct LGP30 *m = make_machine(&stub);
    uint16_t addr = lgp30_address_from_track_sector(1, 0);
    m->accumulator = 0x00000006u; /* 3 at q=30: magnitude=3, word=3<<1 */
    m->memory[addr] = 0x40000000u; /* 2 at q=2 */
    m->memory[0] = lgp30_make_instruction(0x7, addr);
    lgp30_step(m);
    ASSERT_EQ_U32(m->accumulator, 0x00000002u, "truncates to 4, not 6, at q=32 (magnitude=1, word=1<<1)");
    lgp30_destroy(m);
}

TEST(divide_manual_example) {
    /* Manual's own worked example (p.27): 3 at q=3 (acc) divided by 2 at
     * q=2 (mem) = 1.5 at q=1. */
    struct stub_io_ctx stub = {0};
    struct LGP30 *m = make_machine(&stub);
    uint16_t addr = lgp30_address_from_track_sector(1, 0);
    m->accumulator = 0x30000000u; /* 3 at q=3 */
    m->memory[addr] = 0x40000000u; /* 2 at q=2 */
    m->memory[0] = lgp30_make_instruction(0x5, addr); /* d addr */
    lgp30_step(m);
    ASSERT_EQ_U32(m->accumulator, 0x60000000u, "3/2 at q=3-2 = 1.5 at q=1");
    lgp30_destroy(m);
}

TEST(divide_overflow_halts) {
    struct stub_io_ctx stub = {0};
    struct LGP30 *m = make_machine(&stub);
    uint16_t addr = lgp30_address_from_track_sector(1, 0);
    m->accumulator = 0x7FFFFFFEu; /* magnitude close to 1 */
    m->memory[addr] = 0x00000002u; /* tiny divisor -> quotient >= 1 */
    m->memory[0] = lgp30_make_instruction(0x5, addr);
    enum lgp30_status s = lgp30_step(m);
    ASSERT_EQ_INT(s, LGP30_OVERFLOW, "quotient >= 1 overflows");
    lgp30_destroy(m);
}

TEST(n_multiply_retains_low_bits) {
    /* Motivated by the manual's own example (p.21): multiplying 1 at
     * q=20 by 1 at q=20 in memory. M multiply loses the result (falls
     * below the top 30 bits); N multiply retains it. This checks the
     * mechanical bit placement, not the "q" bookkeeping (a programmer
     * convention external to the hardware -- see op_n_multiply). */
    struct stub_io_ctx stub = {0};
    struct LGP30 *m = make_machine(&stub);
    uint16_t addr = lgp30_address_from_track_sector(1, 0);
    word32 one_at_q20 = 1024u << 1; /* magnitude 2^10, sign 0 */
    m->accumulator = one_at_q20;
    m->memory[addr] = one_at_q20;
    m->memory[0] = lgp30_make_instruction(0x6, addr); /* n addr */
    lgp30_step(m);
    ASSERT_EQ_U32(m->accumulator, (1024u * 1024u) << 1, "low 31 bits of the product");
    lgp30_destroy(m);
}

TEST(input_shifts_accumulator_until_stop_code) {
    /* A single i 0000 autonomously reads characters across several
     * lgp30_step() calls until a stop code (32, "Cond Stop") ends the
     * word -- not one i 0000 per character (p.34). */
    struct stub_io_ctx stub = {0};
    stub.input[0] = 0xA;
    stub.input[1] = 0xB;
    stub.input[2] = 32; /* stop code */
    stub.input_count = 3;
    struct LGP30 *m = make_machine(&stub);
    m->memory[0] = lgp30_make_instruction(0x4, 0); /* i 0000 */
    m->memory[1] = lgp30_make_instruction(0x0, 0); /* z 0000 */

    lgp30_step(m);
    ASSERT_EQ_U32(m->accumulator, 0xAu, "first 4-bit char shifted in");
    ASSERT_TRUE(m->input_in_progress, "still mid-sequence");

    lgp30_step(m);
    ASSERT_EQ_U32(m->accumulator, 0xABu, "second char pushes the first left");
    ASSERT_TRUE(m->input_in_progress, "still mid-sequence");

    enum lgp30_status s = lgp30_step(m);
    ASSERT_EQ_INT(s, LGP30_RUNNING, "stop code ends the sequence normally");
    ASSERT_TRUE(!m->input_in_progress, "sequence finished");
    ASSERT_EQ_U32(m->accumulator, 0xABu, "the stop code itself is not shifted in");
    ASSERT_EQ_U32(m->counter, 1, "counter now at the instruction after i 0000");

    enum lgp30_status s2 = lgp30_step(m);
    ASSERT_EQ_INT(s2, LGP30_STOPPED, "execution reached z 0000 after i 0000");
    lgp30_destroy(m);
}

TEST(input_word_completes_after_eight_characters) {
    struct stub_io_ctx stub = {0};
    for (int i = 0; i < 8; i++) {
        stub.input[i] = (i + 1) & 0xF;
    }
    stub.input_count = 8;
    struct LGP30 *m = make_machine(&stub);
    m->memory[0] = lgp30_make_instruction(0x4, 0);

    for (int i = 0; i < 8; i++) {
        lgp30_step(m);
    }
    ASSERT_TRUE(!m->input_in_progress, "word full after 8 characters, no stop code needed");
    ASSERT_EQ_U32(m->accumulator, 0x12345678u, "8 nibbles shifted in, MSB first");
    ASSERT_EQ_U32(m->counter, 1, "counter advanced past i 0000");
    lgp30_destroy(m);
}

TEST(input_blocks_and_retries) {
    struct stub_io_ctx stub = {0};
    stub.block_once = true;
    stub.input[0] = 0x5;
    stub.input[1] = 32; /* stop code */
    stub.input_count = 2;
    struct LGP30 *m = make_machine(&stub);
    m->memory[0] = lgp30_make_instruction(0x4, 0);

    enum lgp30_status s1 = lgp30_step(m);
    ASSERT_EQ_INT(s1, LGP30_WAITING_FOR_IO, "blocks on first attempt");
    ASSERT_TRUE(m->input_in_progress, "sequence pending, not abandoned");

    enum lgp30_status s2 = lgp30_step(m);
    ASSERT_EQ_INT(s2, LGP30_RUNNING, "succeeds on retry");
    ASSERT_EQ_U32(m->accumulator, 0x5u, "char shifted in on retry");

    lgp30_step(m); /* consumes the stop code */
    ASSERT_TRUE(!m->input_in_progress, "sequence complete");
    lgp30_destroy(m);
}

TEST(print_writes_track_bits_as_code) {
    /* "p 2000 has 010100 in the track bits" (p.33): decimal track 20,
     * sector 0 -> track bits = 20 = 0b010100. Print must not touch
     * memory, the accumulator, or the counter otherwise. */
    struct stub_io_ctx stub = {0};
    struct LGP30 *m = make_machine(&stub);
    uint16_t addr = lgp30_address_from_track_sector(20, 0);
    m->accumulator = 0x11111111u;
    m->memory[0] = lgp30_make_instruction(0x8, addr);
    lgp30_step(m);
    ASSERT_EQ_INT(stub.output_count, 1, "one char written");
    ASSERT_EQ_INT(stub.output[0], 20, "track bits used as the raw code");
    ASSERT_EQ_U32(m->accumulator, 0x11111111u, "print doesn't touch acc");
    lgp30_destroy(m);
}

TEST(inspection_reads_registers_and_status) {
    struct stub_io_ctx stub = {0};
    struct LGP30 *m = make_machine(&stub);
    m->accumulator = 0xCAFEBABEu;
    m->counter = 42;
    m->instruction_register = 0x11111111u;
    ASSERT_EQ_U32(lgp30_read_register(m, LGP30_REG_ACCUMULATOR), 0xCAFEBABEu, "accumulator");
    ASSERT_EQ_U32(lgp30_read_register(m, LGP30_REG_COUNTER), 42u, "counter");
    ASSERT_EQ_U32(lgp30_read_register(m, LGP30_REG_INSTRUCTION_REGISTER), 0x11111111u, "instr reg");
    ASSERT_EQ_INT(lgp30_get_status(m), LGP30_RUNNING, "status");
    lgp30_destroy(m);
}

TEST(inspection_memory_peek_does_not_charge_drum_time) {
    struct stub_io_ctx stub = {0};
    struct LGP30 *m = make_machine(&stub);
    uint16_t addr = lgp30_address_from_track_sector(5, 5);
    m->memory[addr] = 0xABCDEFu;
    uint64_t before = m->drum_ticks;
    ASSERT_EQ_U32(lgp30_read_memory(m, addr), 0xABCDEFu, "reads the raw word");
    ASSERT_EQ_U32(m->drum_ticks, before, "peeking doesn't advance the drum clock");
    lgp30_destroy(m);
}

TEST(breakpoints_set_clear_and_survive_reset) {
    struct stub_io_ctx stub = {0};
    struct LGP30 *m = make_machine(&stub);
    uint16_t addr = lgp30_address_from_track_sector(1, 0);
    ASSERT_TRUE(!lgp30_is_breakpoint(m, addr), "none set initially");
    lgp30_set_breakpoint(m, addr);
    ASSERT_TRUE(lgp30_is_breakpoint(m, addr), "now set");
    lgp30_reset(m);
    ASSERT_TRUE(lgp30_is_breakpoint(m, addr), "reset doesn't clear breakpoints");
    lgp30_clear_breakpoint(m, addr);
    ASSERT_TRUE(!lgp30_is_breakpoint(m, addr), "explicitly cleared");
    lgp30_destroy(m);
}

int main(void) {
    RUN_TEST(stop_halts);
    RUN_TEST(bring_loads_accumulator);
    RUN_TEST(hold_and_store_keeps_accumulator);
    RUN_TEST(clear_and_store_clears_accumulator);
    RUN_TEST(store_address_preserves_opcode_bits);
    RUN_TEST(return_address_stores_counter_plus_one);
    RUN_TEST(unconditional_transfer_jumps);
    RUN_TEST(test_branches_when_negative);
    RUN_TEST(test_falls_through_when_nonnegative);
    RUN_TEST(extract_bitwise_and);
    RUN_TEST(add_and_subtract_complement_example);
    RUN_TEST(add_overflow_halts);
    RUN_TEST(m_multiply_manual_example);
    RUN_TEST(m_multiply_truncation_example);
    RUN_TEST(divide_manual_example);
    RUN_TEST(divide_overflow_halts);
    RUN_TEST(n_multiply_retains_low_bits);
    RUN_TEST(input_shifts_accumulator_until_stop_code);
    RUN_TEST(input_word_completes_after_eight_characters);
    RUN_TEST(input_blocks_and_retries);
    RUN_TEST(print_writes_track_bits_as_code);
    RUN_TEST(inspection_reads_registers_and_status);
    RUN_TEST(inspection_memory_peek_does_not_charge_drum_time);
    RUN_TEST(breakpoints_set_clear_and_survive_reset);
    return test_util_report();
}
