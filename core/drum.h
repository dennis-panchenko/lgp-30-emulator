/* Drum rotational timing. Every access to main memory (fetch or operand)
 * pays a wait for the target sector to rotate under the read head — this
 * is the emulator's central "feel" constraint (see ABOUT.md). Register-
 * only arithmetic (accumulator, no memory touch) pays nothing here. */
#ifndef LGP30_DRUM_H
#define LGP30_DRUM_H

#include "types.h"

#define LGP30_SECTORS_PER_REVOLUTION 64u

/* 3700 RPM = 17ms/revolution, per the manual (specs table, p.57), stated
 * twice consistently. Ticks are nanoseconds: 17,000,000 / 64 divides
 * exactly (265,625 ns/sector), so sector boundaries never need rounding. */
#define LGP30_TICKS_PER_REVOLUTION 17000000ull
#define LGP30_TICKS_PER_SECTOR (LGP30_TICKS_PER_REVOLUTION / LGP30_SECTORS_PER_REVOLUTION)

/* Advances machine->drum_ticks until the read head reaches target_sector
 * (0 if it's already there, up to just under one full revolution in the
 * worst case). Returns the number of ticks the wait consumed. Track plays
 * no part here — all 64 tracks rotate together; track only selects which
 * physical head reads, not when. */
uint64_t drum_wait_for_sector(struct LGP30 *machine, uint8_t target_sector);

/* Both pay drum_wait_for_sector() for address's sector. drum_write masks
 * the spacer bit (see lgp30_mask_spacer) since a drum word is only 31
 * meaningful bits; drum_read returns whatever is stored, spacer included. */
word32 drum_read(struct LGP30 *machine, uint16_t address);
void drum_write(struct LGP30 *machine, uint16_t address, word32 value);

#endif
