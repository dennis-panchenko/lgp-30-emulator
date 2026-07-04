#include "drum.h"

uint64_t drum_wait_for_sector(struct LGP30 *machine, uint8_t target_sector) {
    uint64_t current_position = machine->drum_ticks % LGP30_TICKS_PER_REVOLUTION;
    uint64_t target_position = (uint64_t)target_sector * LGP30_TICKS_PER_SECTOR;

    uint64_t wait = (target_position >= current_position)
        ? (target_position - current_position)
        : (LGP30_TICKS_PER_REVOLUTION - current_position + target_position);

    machine->drum_ticks += wait;
    return wait;
}

word32 drum_read(struct LGP30 *machine, uint16_t address) {
    drum_wait_for_sector(machine, lgp30_sector_of(address));
    return machine->memory[address];
}

void drum_write(struct LGP30 *machine, uint16_t address, word32 value) {
    drum_wait_for_sector(machine, lgp30_sector_of(address));
    machine->memory[address] = lgp30_mask_spacer(value);
}
