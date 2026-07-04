/* Shared machine types. No platform headers here (see ABOUT.md) — this
 * must compile cleanly across every backend that links against this core. */
#ifndef LGP30_TYPES_H
#define LGP30_TYPES_H

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef uint32_t word32;
static_assert(sizeof(word32) == 4, "word32 must be exactly 32 bits");

#define LGP30_MEMORY_WORDS 4096u

/* Forward-declared, not included: struct LGP30 only ever holds a pointer
 * to an io_ops, so types.h doesn't need to know its layout (io.h doesn't
 * need to know struct LGP30's layout either — the two are independent). */
struct io_ops;

enum lgp30_status {
    LGP30_RUNNING,
    LGP30_STOPPED,          /* z (stop) instruction executed */
    LGP30_OVERFLOW,         /* add/subtract/divide overflow */
    LGP30_WAITING_FOR_IO,   /* print/input instruction blocked on the host */
};

struct LGP30 {
    word32 memory[LGP30_MEMORY_WORDS];

    /* Accumulator is a genuine 32-bit register: sign + 30 magnitude bits +
     * spacer. A word written to the drum is only 31 meaningful bits — see
     * lgp30_mask_spacer() below, used by drum_write(), never by the
     * accumulator itself. */
    word32 accumulator;

    uint16_t counter;              /* 12-bit program counter: track<<6|sector */
    word32 instruction_register;   /* currently executing instruction word */

    uint64_t drum_ticks;           /* monotonic drum position/time, see drum.h */

    /* A single `i 0000` instruction autonomously shifts characters in
     * until an 8th character or a stop code, not just one character (see
     * cpu.c's op_input_continue) -- this tracks that multi-step sequence
     * across separate lgp30_step() calls, one character per call. */
    bool input_in_progress;
    uint8_t input_shift_count;

    enum lgp30_status status;
    const struct io_ops *io;

    /* Debugger breakpoints. Survive lgp30_reset() -- resetting
     * execution state shouldn't discard debugging setup. */
    bool breakpoints[LGP30_MEMORY_WORDS];
};

/* --- Instruction word bit layout ---
 * Confirmed from the manual's bit diagram (p.10), translated to 0-indexed
 * LSB-first for a uint32_t. See ABOUT.md for the full derivation.
 *
 *   bit 31       sign
 *   bits 20..17  opcode  (4 bits)
 *   bits 14..9   track   (6 bits)
 *   bits 8..3    sector  (6 bits)
 *   bit 0        spacer  (always 0 on the drum)
 */

static inline uint8_t lgp30_opcode(word32 instruction) {
    return (uint8_t)((instruction >> 17) & 0xFu);
}

/* 12-bit address field: track (upper 6 bits) concatenated with sector
 * (lower 6 bits), i.e. track*64 + sector. */
static inline uint16_t lgp30_instruction_address(word32 instruction) {
    return (uint16_t)((instruction >> 3) & 0xFFFu);
}

static inline uint8_t lgp30_track_of(uint16_t address) {
    return (uint8_t)((address >> 6) & 0x3Fu);
}

static inline uint8_t lgp30_sector_of(uint16_t address) {
    return (uint8_t)(address & 0x3Fu);
}

static inline uint16_t lgp30_address_from_track_sector(uint8_t track, uint8_t sector) {
    return (uint16_t)(((uint16_t)(track & 0x3Fu) << 6) | (sector & 0x3Fu));
}

static inline word32 lgp30_make_instruction(uint8_t opcode, uint16_t address) {
    return ((word32)(opcode & 0xFu) << 17) | ((word32)(address & 0xFFFu) << 3);
}

/* Force bit 0 (spacer) to 0. Every drum_write() must apply this; the
 * accumulator itself must not (see 1957 manual p.34 — a 4-bit input shift
 * can transiently leave a 1 there, and storing it to memory is what
 * clears it). */
static inline word32 lgp30_mask_spacer(word32 word) {
    return word & ~(word32)0x1u;
}

/* --- Number representation ---
 * Negative numbers are stored as the two's complement of the 31-bit
 * sign+magnitude field (word bits 31..1; bit 0 is the spacer), NOT plain
 * sign-magnitude. Confirmed against the manual's own example (p.27):
 * "6 at q=4" is 0|011000...0; "-6 at q=4" is 1|101000...0, which is
 * exactly ~(011000...0)+1, not the same magnitude bits with only the
 * sign flipped. Shared by cpu.c (arithmetic ops) and core/asm/ (the `dw`
 * data-word directive) — single source of truth, don't hand-roll. */

static inline int64_t lgp30_word_to_signed(word32 w) {
    uint32_t v31 = w >> 1;
    if (v31 & 0x40000000u) {
        return (int64_t)v31 - (int64_t)0x80000000u;
    }
    return (int64_t)v31;
}

/* value must be in [-2^30, 2^30-1]; caller is responsible for the
 * overflow check before calling this. */
static inline word32 lgp30_word_from_signed(int64_t value) {
    uint32_t v31 = (uint32_t)value & 0x7FFFFFFFu;
    return v31 << 1;
}

/* --- LGP-30 hex digit alphabet: 0-9, f g j k q w for 10-15 ---
 * NOT used for address notation (see below) or for opcode mnemonic
 * letters, which is a separate, unrelated single-letter-per-opcode
 * mapping (see core/asm/). This alphabet is for representing raw 32-bit
 * word *contents* compactly by hand (e.g. a future register/word hex
 * dump) — kept here as a correctly-sourced utility even though nothing
 * in v1 wires it up yet. */

static inline int lgp30_hex_digit_value(char c) {
    switch (c) {
        case '0': return 0;
        case '1': return 1;
        case '2': return 2;
        case '3': return 3;
        case '4': return 4;
        case '5': return 5;
        case '6': return 6;
        case '7': return 7;
        case '8': return 8;
        case '9': return 9;
        case 'f': case 'F': return 10;
        case 'g': case 'G': return 11;
        case 'j': case 'J': return 12;
        case 'k': case 'K': return 13;
        case 'q': case 'Q': return 14;
        case 'w': case 'W': return 15;
        default: return -1;
    }
}

static inline char lgp30_hex_digit_char(unsigned value) {
    static const char digits[16] = "0123456789fgjkqw";
    return digits[value & 0xFu];
}

/* --- Address notation ---
 * The manual writes an address in assembly-mnemonic form as 4 DECIMAL
 * digits: the first two are the track (00-63), the last two are the
 * sector (00-63) — e.g. "c 2710" means opcode c, track=27, sector=10.
 * Confirmed two ways against the manual's own text, not assumed: (1) the
 * worked example "p 2000" decodes to track-bits 010100 = 20 decimal — a
 * hex-pair reading of "20" would give track 32 (0x20) instead, which
 * does not match; (2) the manual gives "0008 through 6363" as the valid
 * address range for a bootstrap fill, which only makes sense if track
 * and sector are each independently 00-63 in decimal — a hex-digit-pair
 * reading would cap out at "3f3f". This is plain decimal, NOT the
 * LGP-30 hex alphabet above; that alphabet is reserved for a different,
 * v1-out-of-scope purpose (writing raw word contents by hand for
 * bootstrap/Flexowriter entry, per the manual's "Instruction
 * Representation" section, e.g. "c 2710" as raw hex is "c1g28"). */

static inline void lgp30_format_address(uint16_t address, char out[5]) {
    uint8_t track = lgp30_track_of(address);
    uint8_t sector = lgp30_sector_of(address);
    out[0] = (char)('0' + track / 10);
    out[1] = (char)('0' + track % 10);
    out[2] = (char)('0' + sector / 10);
    out[3] = (char)('0' + sector % 10);
    out[4] = '\0';
}

/* Parses exactly 4 characters at text[0..3] (does not require a NUL at
 * text[4]). Returns false on a non-digit or an out-of-range track/sector
 * (either pair > 63). */
static inline bool lgp30_parse_address(const char *text, uint16_t *out_address) {
    for (int i = 0; i < 4; i++) {
        if (text[i] < '0' || text[i] > '9') {
            return false;
        }
    }
    int track = (text[0] - '0') * 10 + (text[1] - '0');
    int sector = (text[2] - '0') * 10 + (text[3] - '0');
    if (track > 63 || sector > 63) {
        return false;
    }
    *out_address = lgp30_address_from_track_sector((uint8_t)track, (uint8_t)sector);
    return true;
}

#endif
