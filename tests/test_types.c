#include "test_util.h"
#include "../core/types.h"

TEST(bit_layout_roundtrips) {
    word32 instr = lgp30_make_instruction(0xA, 0x123);
    ASSERT_EQ_U32(lgp30_opcode(instr), 0xA, "opcode roundtrip");
    ASSERT_EQ_U32(lgp30_instruction_address(instr), 0x123, "address roundtrip");
}

TEST(track_sector_split) {
    uint16_t addr = lgp30_address_from_track_sector(32, 0);
    ASSERT_EQ_U32(lgp30_track_of(addr), 32, "track");
    ASSERT_EQ_U32(lgp30_sector_of(addr), 0, "sector");
    ASSERT_EQ_U32(addr, 32 * 64, "packed address = track*64+sector");
}

TEST(spacer_masking) {
    ASSERT_EQ_U32(lgp30_mask_spacer(0xFFFFFFFFu), 0xFFFFFFFEu, "clears bit 0 only");
    ASSERT_EQ_U32(lgp30_mask_spacer(0u), 0u, "already clear");
}

TEST(hex_digit_alphabet) {
    ASSERT_EQ_INT(lgp30_hex_digit_value('0'), 0, "'0'");
    ASSERT_EQ_INT(lgp30_hex_digit_value('9'), 9, "'9'");
    ASSERT_EQ_INT(lgp30_hex_digit_value('f'), 10, "'f'");
    ASSERT_EQ_INT(lgp30_hex_digit_value('g'), 11, "'g'");
    ASSERT_EQ_INT(lgp30_hex_digit_value('j'), 12, "'j'");
    ASSERT_EQ_INT(lgp30_hex_digit_value('k'), 13, "'k'");
    ASSERT_EQ_INT(lgp30_hex_digit_value('q'), 14, "'q'");
    ASSERT_EQ_INT(lgp30_hex_digit_value('w'), 15, "'w'");
    ASSERT_EQ_INT(lgp30_hex_digit_value('a'), -1, "'a' is not a valid LGP-30 digit");

    for (unsigned v = 0; v < 16; v++) {
        char c = lgp30_hex_digit_char(v);
        ASSERT_EQ_INT(lgp30_hex_digit_value(c), (int)v, "char/value roundtrip");
    }
}

TEST(address_notation_example_from_manual_p2000) {
    /* "p 2000" decodes to track-bits 010100 = 20 decimal per the manual's
     * own worked example (p.33) -- decimal track 20, sector 0, not a
     * hex-pair reading (which would give track 32). */
    uint16_t addr;
    ASSERT_TRUE(lgp30_parse_address("2000", &addr), "parses");
    ASSERT_EQ_U32(lgp30_track_of(addr), 20, "track 20 (decimal)");
    ASSERT_EQ_U32(lgp30_sector_of(addr), 0, "sector 0");

    char text[5];
    lgp30_format_address(addr, text);
    ASSERT_TRUE(text[0] == '2' && text[1] == '0' && text[2] == '0' && text[3] == '0',
                "formats back to \"2000\"");
}

TEST(address_notation_example_c2710) {
    /* "c 2710" per the manual's "Instruction Representation" section:
     * decimal track 27, sector 10. */
    uint16_t addr;
    ASSERT_TRUE(lgp30_parse_address("2710", &addr), "parses");
    ASSERT_EQ_U32(lgp30_track_of(addr), 27, "track 27");
    ASSERT_EQ_U32(lgp30_sector_of(addr), 10, "sector 10");
}

TEST(address_notation_rejects_out_of_range) {
    uint16_t addr;
    /* "6400" -> track = 64, out of the 0-63 range. */
    ASSERT_TRUE(!lgp30_parse_address("6400", &addr), "track > 63 rejected");
    /* invalid digit 'a' is not a decimal digit */
    ASSERT_TRUE(!lgp30_parse_address("a000", &addr), "non-digit rejected");
}

int main(void) {
    RUN_TEST(bit_layout_roundtrips);
    RUN_TEST(track_sector_split);
    RUN_TEST(spacer_masking);
    RUN_TEST(hex_digit_alphabet);
    RUN_TEST(address_notation_example_from_manual_p2000);
    RUN_TEST(address_notation_example_c2710);
    RUN_TEST(address_notation_rejects_out_of_range);
    return test_util_report();
}
