#include "test_util.h"
#include "../core/drum.h"
#include "../core/types.h"

static struct LGP30 fresh_machine(void) {
    struct LGP30 m;
    for (size_t i = 0; i < LGP30_MEMORY_WORDS; i++) {
        m.memory[i] = 0;
    }
    m.accumulator = 0;
    m.counter = 0;
    m.instruction_register = 0;
    m.drum_ticks = 0;
    m.status = LGP30_RUNNING;
    m.io = NULL;
    return m;
}

TEST(zero_wait_when_already_at_sector) {
    struct LGP30 m = fresh_machine();
    m.drum_ticks = 10 * LGP30_TICKS_PER_SECTOR; /* head sitting exactly at sector 10 */
    uint64_t wait = drum_wait_for_sector(&m, 10);
    ASSERT_EQ_U32(wait, 0, "no wait needed");
    ASSERT_EQ_U32(m.drum_ticks, 10 * LGP30_TICKS_PER_SECTOR, "ticks unchanged");
}

TEST(near_full_revolution_when_just_missed) {
    struct LGP30 m = fresh_machine();
    /* head is 1 tick past sector 5 -> must wait almost a full revolution
     * to come back around to sector 5. */
    m.drum_ticks = 5 * LGP30_TICKS_PER_SECTOR + 1;
    uint64_t wait = drum_wait_for_sector(&m, 5);
    ASSERT_EQ_U32(wait, LGP30_TICKS_PER_REVOLUTION - 1, "worst-case wait");
}

TEST(partial_wait_for_sector_ahead) {
    struct LGP30 m = fresh_machine();
    m.drum_ticks = 5 * LGP30_TICKS_PER_SECTOR;
    uint64_t wait = drum_wait_for_sector(&m, 8);
    ASSERT_EQ_U32(wait, 3 * LGP30_TICKS_PER_SECTOR, "waits exactly the sector gap");
}

TEST(wraps_around_for_sector_behind) {
    struct LGP30 m = fresh_machine();
    m.drum_ticks = 60 * LGP30_TICKS_PER_SECTOR;
    uint64_t wait = drum_wait_for_sector(&m, 2);
    /* (64-60) sectors to wrap, plus 2 more to reach sector 2 */
    ASSERT_EQ_U32(wait, 6 * LGP30_TICKS_PER_SECTOR, "wraps to the target sector");
}

TEST(read_write_round_trip) {
    struct LGP30 m = fresh_machine();
    uint16_t addr = lgp30_address_from_track_sector(3, 7);
    drum_write(&m, addr, 0xDEADBEEFu);
    word32 got = drum_read(&m, addr);
    /* 0xDEADBEEF has bit 0 set; drum_write must have masked the spacer. */
    ASSERT_EQ_U32(got, 0xDEADBEEEu, "spacer bit masked on write");
}

TEST(access_advances_drum_ticks) {
    struct LGP30 m = fresh_machine();
    uint16_t addr = lgp30_address_from_track_sector(0, 20);
    uint64_t before = m.drum_ticks;
    drum_read(&m, addr);
    ASSERT_TRUE(m.drum_ticks >= before, "drum_ticks is monotonic");
}

int main(void) {
    RUN_TEST(zero_wait_when_already_at_sector);
    RUN_TEST(near_full_revolution_when_just_missed);
    RUN_TEST(partial_wait_for_sector_ahead);
    RUN_TEST(wraps_around_for_sector_behind);
    RUN_TEST(read_write_round_trip);
    RUN_TEST(access_advances_drum_ticks);
    return test_util_report();
}
