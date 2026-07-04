#include "cpu.h"
#include "drum.h"
#include "io.h"

#include <stdlib.h>

struct LGP30 *lgp30_create(void) {
    struct LGP30 *m = malloc(sizeof(*m));
    if (m == NULL) {
        return NULL;
    }
    for (size_t i = 0; i < LGP30_MEMORY_WORDS; i++) {
        m->memory[i] = 0;
        m->breakpoints[i] = false;
    }
    m->accumulator = 0;
    m->counter = 0;
    m->instruction_register = 0;
    m->drum_ticks = 0;
    m->input_in_progress = false;
    m->input_shift_count = 0;
    m->status = LGP30_RUNNING;
    m->io = NULL;
    return m;
}

void lgp30_destroy(struct LGP30 *m) {
    free(m);
}

void lgp30_reset(struct LGP30 *m) {
    m->accumulator = 0;
    m->counter = 0;
    m->instruction_register = 0;
    m->drum_ticks = 0;
    m->input_in_progress = false;
    m->input_shift_count = 0;
    m->status = LGP30_RUNNING;
}

void lgp30_attach_io(struct LGP30 *m, const struct io_ops *io) {
    m->io = io;
}

/* to_signed/from_signed live in types.h as lgp30_word_to_signed/
 * lgp30_word_from_signed -- shared with core/asm/'s `dw` directive. */
#define to_signed lgp30_word_to_signed
#define from_signed lgp30_word_from_signed

/* M/N multiply and divide work on true (sign, magnitude) pairs, not the
 * two's-complement bit pattern directly -- confirmed by the manual's
 * positive-operand worked examples for M multiply and divide, and by the
 * fact that N multiply's result explicitly has "no true sign bit" (see
 * op_n_multiply), which only makes sense if the hardware derives the
 * result's sign separately via XOR rather than getting it "for free" from
 * a raw two's-complement multiply. Note: mag can be exactly 2^30 for the
 * boundary value -2^30 (the most negative representable number, i.e.
 * -1.0 in the fractional interpretation) -- a known edge case in a
 * machine whose normal magnitude range is [0, 2^30-1]; not specially
 * handled here. */
static void to_sign_magnitude(word32 w, bool *sign, uint32_t *mag) {
    int64_t v = to_signed(w);
    *sign = v < 0;
    *mag = (uint32_t)(*sign ? -v : v);
}

#define ADDRESS_FIELD_MASK ((word32)0xFFFu << 3)

/* Read-modify-write helpers for the "store" family (y, r): one drum
 * access covers both the read of the old word and the write of the
 * merged result, since it's the same sector passing under the head once. */
static word32 rmw_peek(struct LGP30 *m, uint16_t addr) {
    drum_wait_for_sector(m, lgp30_sector_of(addr));
    return m->memory[addr];
}

static void rmw_poke(struct LGP30 *m, uint16_t addr, word32 value) {
    m->memory[addr] = lgp30_mask_spacer(value);
}

static void op_stop(struct LGP30 *m) {
    m->status = LGP30_STOPPED;
}

static void op_bring(struct LGP30 *m, uint16_t addr) {
    m->accumulator = drum_read(m, addr);
}

/* "y 1000 means replace the contents of the address portion of the word
 * in location 1000 with the contents of the address portion of the word
 * in the accumulator. The contents of the accumulator is unaffected."
 * (p.20) -- the classic self-modifying-address idiom. */
static void op_store_address(struct LGP30 *m, uint16_t addr) {
    word32 old = rmw_peek(m, addr);
    word32 merged = (old & ~ADDRESS_FIELD_MASK) | (m->accumulator & ADDRESS_FIELD_MASK);
    rmw_poke(m, addr, merged);
}

/* "If the instruction r 3050 is located in 1013, it stores the address
 * 1015 in location 3050" (p.18) -- i.e. mem[addr]'s address field becomes
 * (address of this r instruction) + 2: one past the transfer instruction
 * conventionally placed right after r, so execution resumes just past
 * that transfer once the callee jumps back here. lgp30_step has already
 * advanced m->counter past this instruction by the time we get here, so
 * that's just m->counter + 1. Accumulator is unaffected (confirmed by the
 * manual's own example, where the accumulator holds a value that must
 * survive through the r instruction to be used afterward). */
static void op_return_address(struct LGP30 *m, uint16_t addr) {
    word32 old = rmw_peek(m, addr);
    uint16_t return_point = (uint16_t)((m->counter + 1) & 0xFFFu);
    word32 merged = (old & ~ADDRESS_FIELD_MASK) | ((word32)return_point << 3);
    rmw_poke(m, addr, merged);
}

/* "Cond Stop" on the Flexowriter keyboard code table (p.33) -- binary
 * 100000 = 32 -- matches ABOUT.md/the manual's "a stop code (100000) on
 * tape halts the reader" exactly. */
#define INPUT_STOP_CODE 32

/* A single `i 0000` autonomously reads characters until the 8th (the
 * accumulator is 32 bits = 8 x 4-bit input shifts) or a stop code,
 * "reciprocal in function to... p 0000 and i 0000" (p.34): the pair
 * starts the reader and stops computation; the stop code stops the
 * reader and resumes computation at the instruction after i 0000. Called
 * once per lgp30_step() -- one character per call -- so a program that
 * issues a single `i 0000` still spans several steps, preserving the
 * step-driven contract instead of blocking in a tight loop. */
static void op_input_continue(struct LGP30 *m) {
    int c = 0;
    int rc = m->io->read_char(m->io->ctx, &c);
    if (rc == 1) {
        m->status = LGP30_WAITING_FOR_IO; /* retried next lgp30_step() */
        return;
    }
    if (rc == -1) {
        /* EOF/error: not a case the manual covers (an operator would just
         * stop the reader by hand); halting here is an emulator-level
         * convenience so a batch run terminates instead of spinning. */
        m->input_in_progress = false;
        m->status = LGP30_STOPPED;
        return;
    }
    if ((c & 0x3F) == INPUT_STOP_CODE) {
        m->input_in_progress = false; /* word ends here; not shifted in */
        return;
    }
    m->accumulator = (m->accumulator << 4) | ((word32)c & 0xFu);
    m->input_shift_count++;
    if (m->input_shift_count >= 8) {
        m->input_in_progress = false; /* word full */
    }
}

/* Print's operand is not a memory address: the address's 6-bit track
 * field is the literal Flexowriter output code (p.33: "p 2000 has 010100
 * in the track bits, which is the code for a back space"). Print never
 * touches memory, the accumulator, or the counter. */
static void op_print(struct LGP30 *m, uint16_t addr) {
    uint8_t code = lgp30_track_of(addr);
    m->io->write_char(m->io->ctx, code);
}

/* "e 2000... put zeroes in the accumulator wherever there are zeroes in
 * the word in location 2000, but otherwise leave the accumulator
 * unchanged" (p.21) -- plain bitwise AND, mask word (mem[addr])
 * unaffected. */
static void op_extract(struct LGP30 *m, uint16_t addr) {
    word32 mask = drum_read(m, addr);
    m->accumulator &= mask;
}

static void op_unconditional_transfer(struct LGP30 *m, uint16_t addr) {
    m->counter = addr;
}

static void op_test(struct LGP30 *m, uint16_t addr) {
    if (m->accumulator & 0x80000000u) {
        m->counter = addr;
    }
}

static void op_hold_and_store(struct LGP30 *m, uint16_t addr) {
    drum_write(m, addr, m->accumulator);
}

/* "c 1002 means replace the contents of memory location 1002 with the
 * contents of the accumulator and replace the contents of the
 * accumulator with zero." (p.18) */
static void op_clear_and_store(struct LGP30 *m, uint16_t addr) {
    drum_write(m, addr, m->accumulator);
    m->accumulator = 0;
}

static bool overflows_31bit(int64_t value) {
    return value > 0x3FFFFFFFLL || value < -0x40000000LL;
}

static void op_add(struct LGP30 *m, uint16_t addr) {
    int64_t result = to_signed(m->accumulator) + to_signed(drum_read(m, addr));
    if (overflows_31bit(result)) {
        m->status = LGP30_OVERFLOW;
        return;
    }
    m->accumulator = from_signed(result);
}

static void op_subtract(struct LGP30 *m, uint16_t addr) {
    int64_t result = to_signed(m->accumulator) - to_signed(drum_read(m, addr));
    if (overflows_31bit(result)) {
        m->status = LGP30_OVERFLOW;
        return;
    }
    m->accumulator = from_signed(result);
}

/* "M multiply... keep[s] the most significant 30 bits + sign" -- a plain
 * fractional multiply of two magnitudes in [0,1), which can never reach
 * 1, so (per the manual) M multiply can never overflow. Truncates rather
 * than rounds -- confirmed by the manual's own "Truncation" example
 * (p.26: 3 at q=30 times 2 at q=2 gives 4 at q=32, not 6, because the
 * low bits of the product are simply dropped). */
static void op_m_multiply(struct LGP30 *m, uint16_t addr) {
    bool sign_a, sign_b;
    uint32_t mag_a, mag_b;
    to_sign_magnitude(m->accumulator, &sign_a, &mag_a);
    to_sign_magnitude(drum_read(m, addr), &sign_b, &mag_b);

    uint64_t product = (uint64_t)mag_a * (uint64_t)mag_b;
    uint32_t result_mag = (uint32_t)(product >> 30) & 0x3FFFFFFFu;
    bool result_sign = (sign_a != sign_b);
    m->accumulator = from_signed(result_sign ? -(int64_t)result_mag : (int64_t)result_mag);
}

/* "n 2000 means multiply the contents of the accumulator by the contents
 * of location 2000 and retain the least significant half of the product
 * in the accumulator... the sign position in this case represents
 * magnitude and not sign." (pp.21,28) -- the low 31 bits of the product
 * are placed directly into the word's sign+magnitude positions, bypassing
 * to_signed()/from_signed() entirely since there is no real sign bit to
 * interpret. Used for left-shifting (multiply by 1 at a high q). */
static void op_n_multiply(struct LGP30 *m, uint16_t addr) {
    bool sign_a, sign_b;
    uint32_t mag_a, mag_b;
    to_sign_magnitude(m->accumulator, &sign_a, &mag_a);
    to_sign_magnitude(drum_read(m, addr), &sign_b, &mag_b);
    (void)sign_a;
    (void)sign_b;

    uint64_t product = (uint64_t)mag_a * (uint64_t)mag_b;
    uint32_t low31 = (uint32_t)(product & 0x7FFFFFFFu);
    m->accumulator = low31 << 1;
}

/* "the result is... rounded to the nearest bit in the thirtieth place...
 * [and] overflow due to division can... occur" (p.27) when the quotient's
 * magnitude would reach 1. Division by zero is not covered by the manual
 * (an operator error on real hardware); treated as overflow here. */
static void op_divide(struct LGP30 *m, uint16_t addr) {
    bool sign_a, sign_b;
    uint32_t mag_a, mag_b;
    to_sign_magnitude(m->accumulator, &sign_a, &mag_a);
    to_sign_magnitude(drum_read(m, addr), &sign_b, &mag_b);

    if (mag_b == 0) {
        m->status = LGP30_OVERFLOW;
        return;
    }

    uint64_t numerator = (uint64_t)mag_a << 30;
    uint64_t q = numerator / mag_b;
    uint64_t rem = numerator % mag_b;
    if (rem * 2 >= mag_b) {
        q++;
    }

    if (q >= (1ull << 30)) {
        m->status = LGP30_OVERFLOW;
        return;
    }

    bool result_sign = (sign_a != sign_b);
    m->accumulator = from_signed(result_sign ? -(int64_t)q : (int64_t)q);
}

enum lgp30_status lgp30_step(struct LGP30 *m) {
    if (m->status == LGP30_STOPPED || m->status == LGP30_OVERFLOW) {
        return m->status;
    }
    m->status = LGP30_RUNNING;

    if (m->input_in_progress) {
        op_input_continue(m);
        return m->status;
    }

    uint16_t fetch_addr = m->counter;
    word32 instr = drum_read(m, fetch_addr);
    m->instruction_register = instr;
    m->counter = (uint16_t)((fetch_addr + 1) & 0xFFFu);

    uint8_t opcode = lgp30_opcode(instr);
    uint16_t addr = lgp30_instruction_address(instr);

    switch (opcode) {
        case 0x0: op_stop(m); break;
        case 0x1: op_bring(m, addr); break;
        case 0x2: op_store_address(m, addr); break;
        case 0x3: op_return_address(m, addr); break;
        case 0x4:
            m->input_in_progress = true;
            m->input_shift_count = 0;
            op_input_continue(m);
            break;
        case 0x5: op_divide(m, addr); break;
        case 0x6: op_n_multiply(m, addr); break;
        case 0x7: op_m_multiply(m, addr); break;
        case 0x8: op_print(m, addr); break;
        case 0x9: op_extract(m, addr); break;
        case 0xA: op_unconditional_transfer(m, addr); break;
        case 0xB: op_test(m, addr); break;
        case 0xC: op_hold_and_store(m, addr); break;
        case 0xD: op_clear_and_store(m, addr); break;
        case 0xE: op_add(m, addr); break;
        case 0xF: op_subtract(m, addr); break;
        default: break;
    }

    return m->status;
}

word32 lgp30_read_register(const struct LGP30 *m, enum lgp30_register reg) {
    switch (reg) {
        case LGP30_REG_ACCUMULATOR: return m->accumulator;
        case LGP30_REG_COUNTER: return m->counter;
        case LGP30_REG_INSTRUCTION_REGISTER: return m->instruction_register;
        default: return 0;
    }
}

word32 lgp30_read_memory(const struct LGP30 *m, uint16_t address) {
    return m->memory[address];
}

enum lgp30_status lgp30_get_status(const struct LGP30 *m) {
    return m->status;
}

void lgp30_set_breakpoint(struct LGP30 *m, uint16_t address) {
    m->breakpoints[address] = true;
}

void lgp30_clear_breakpoint(struct LGP30 *m, uint16_t address) {
    m->breakpoints[address] = false;
}

bool lgp30_is_breakpoint(const struct LGP30 *m, uint16_t address) {
    return m->breakpoints[address];
}
