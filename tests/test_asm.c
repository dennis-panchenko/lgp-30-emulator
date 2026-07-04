#include "test_util.h"
#include "../core/asm/asm.h"
#include "../core/asm/image.h"
#include "../core/types.h"

TEST(assembles_instruction_and_data_word) {
    const char *src =
        "; a tiny program\n"
        "start 1000\n"
        "1000 b 2000  ; bring\n"
        "1001 a 2001\n"
        "2000 dw 5\n"
        "2001 dw -3\n";
    struct asm_result r;
    asm_assemble(src, &r);
    ASSERT_EQ_U32(r.error_count, 0, "no errors");
    ASSERT_TRUE(r.image.has_start_address, "start address set");
    ASSERT_EQ_U32(r.image.start_address, lgp30_address_from_track_sector(10, 0), "start = 1000");

    uint16_t loc1000 = lgp30_address_from_track_sector(10, 0);
    uint16_t loc1001 = lgp30_address_from_track_sector(10, 1);
    uint16_t loc2000 = lgp30_address_from_track_sector(20, 0);
    uint16_t loc2001 = lgp30_address_from_track_sector(20, 1);

    ASSERT_TRUE(r.image.assigned[loc1000], "1000 assigned");
    ASSERT_EQ_U32(lgp30_opcode(r.image.memory[loc1000]), 0x1, "opcode b");
    ASSERT_EQ_U32(lgp30_instruction_address(r.image.memory[loc1000]), loc2000, "operand 2000");

    ASSERT_TRUE(r.image.assigned[loc1001], "1001 assigned");
    ASSERT_EQ_U32(lgp30_opcode(r.image.memory[loc1001]), 0xE, "opcode a");

    ASSERT_EQ_U32(r.image.memory[loc2000], 5u << 1, "dw 5 encodes as +5");
    ASSERT_EQ_U32(lgp30_word_to_signed(r.image.memory[loc2001]), (int64_t)-3, "dw -3 round-trips via to_signed");

    asm_result_free(&r);
}

TEST(blank_lines_and_comments_are_skipped) {
    const char *src = "\n; just a comment\n   \nstart 0000\n";
    struct asm_result r;
    asm_assemble(src, &r);
    ASSERT_EQ_U32(r.error_count, 0, "no errors");
    ASSERT_TRUE(r.image.has_start_address, "start still parsed");
    asm_result_free(&r);
}

TEST(reports_line_and_column_for_bad_address) {
    const char *src = "1000 b 99999\n";
    struct asm_result r;
    asm_assemble(src, &r);
    ASSERT_EQ_U32(r.error_count, 1, "one error");
    ASSERT_EQ_INT(r.errors[0].line, 1, "line 1");
    ASSERT_EQ_INT(r.errors[0].column, 8, "column of the bad operand token");
    asm_result_free(&r);
}

TEST(reports_unknown_opcode_letter) {
    const char *src = "1000 x 2000\n";
    struct asm_result r;
    asm_assemble(src, &r);
    ASSERT_EQ_U32(r.error_count, 1, "one error");
    ASSERT_EQ_INT(r.errors[0].line, 1, "line 1");
    ASSERT_EQ_INT(r.errors[0].column, 6, "column of the opcode token");
    asm_result_free(&r);
}

TEST(rejects_duplicate_location) {
    const char *src = "1000 b 2000\n1000 a 2000\n";
    struct asm_result r;
    asm_assemble(src, &r);
    ASSERT_EQ_U32(r.error_count, 1, "one error");
    ASSERT_EQ_INT(r.errors[0].line, 2, "flagged on the second occurrence");
    asm_result_free(&r);
}

TEST(rejects_out_of_range_dw_value) {
    const char *src = "1000 dw 9999999999\n";
    struct asm_result r;
    asm_assemble(src, &r);
    ASSERT_EQ_U32(r.error_count, 1, "one error");
    asm_result_free(&r);
}

TEST(malformed_line_does_not_crash_and_reports_error) {
    const char *src = "this is not valid\n1000 b 2000\n";
    struct asm_result r;
    asm_assemble(src, &r);
    ASSERT_EQ_U32(r.error_count, 1, "one error on line 1");
    ASSERT_EQ_INT(r.errors[0].line, 1, "line 1");
    /* the valid second line still assembled */
    uint16_t loc1000 = lgp30_address_from_track_sector(10, 0);
    ASSERT_TRUE(r.image.assigned[loc1000], "subsequent valid line still assembled");
    asm_result_free(&r);
}

TEST(image_round_trips_through_text_format) {
    const char *src = "start 1000\n1000 b 2000\n2000 dw 42\n";
    struct asm_result r;
    asm_assemble(src, &r);
    ASSERT_EQ_U32(r.error_count, 0, "assembles cleanly");

    FILE *f = tmpfile();
    ASSERT_TRUE(image_write(&r.image, f), "writes");
    rewind(f);

    struct asm_image loaded;
    ASSERT_TRUE(image_read(&loaded, f), "reads back");
    ASSERT_TRUE(loaded.has_start_address, "start preserved");
    ASSERT_EQ_U32(loaded.start_address, r.image.start_address, "start value preserved");

    uint16_t loc1000 = lgp30_address_from_track_sector(10, 0);
    uint16_t loc2000 = lgp30_address_from_track_sector(20, 0);
    ASSERT_TRUE(loaded.assigned[loc1000] && loaded.assigned[loc2000], "both locations preserved");
    ASSERT_EQ_U32(loaded.memory[loc1000], r.image.memory[loc1000], "instruction word preserved");
    ASSERT_EQ_U32(loaded.memory[loc2000], r.image.memory[loc2000], "data word preserved");

    fclose(f);
    asm_result_free(&r);
}

int main(void) {
    RUN_TEST(assembles_instruction_and_data_word);
    RUN_TEST(blank_lines_and_comments_are_skipped);
    RUN_TEST(reports_line_and_column_for_bad_address);
    RUN_TEST(reports_unknown_opcode_letter);
    RUN_TEST(rejects_duplicate_location);
    RUN_TEST(rejects_out_of_range_dw_value);
    RUN_TEST(malformed_line_does_not_crash_and_reports_error);
    RUN_TEST(image_round_trips_through_text_format);
    return test_util_report();
}
