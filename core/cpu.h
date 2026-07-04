#ifndef LGP30_CPU_H
#define LGP30_CPU_H

#include "types.h"

struct LGP30 *lgp30_create(void);
void lgp30_destroy(struct LGP30 *machine);

/* Clears registers, counter, and drum_ticks and sets status back to
 * RUNNING. Does NOT clear memory[] -- a loaded program survives a reset,
 * only execution state is rewound. */
void lgp30_reset(struct LGP30 *machine);

void lgp30_attach_io(struct LGP30 *machine, const struct io_ops *io);

/* Fetches, decodes, and executes one instruction, advancing the drum
 * clock and (usually) the counter. Returns the resulting status.
 * LGP30_WAITING_FOR_IO is transient: the blocked instruction is retried
 * (not skipped) on the next call. LGP30_STOPPED and LGP30_OVERFLOW are
 * sticky -- further calls are no-ops that just return the same status. */
enum lgp30_status lgp30_step(struct LGP30 *machine);

/* --- Inspection API ---
 * Read-only/setup access for a debugger (the REPL) -- kept general
 * enough for other front-ends to reuse, see ABOUT.md. */

enum lgp30_register {
    LGP30_REG_ACCUMULATOR,
    LGP30_REG_COUNTER,
    LGP30_REG_INSTRUCTION_REGISTER,
};

word32 lgp30_read_register(const struct LGP30 *machine, enum lgp30_register reg);

/* Peeks memory[address] without charging drum rotational-wait time --
 * deliberately different from what the machine itself pays via
 * drum_read(): an external debugger looking at memory isn't the same as
 * the machine accessing it. */
word32 lgp30_read_memory(const struct LGP30 *machine, uint16_t address);

enum lgp30_status lgp30_get_status(const struct LGP30 *machine);

void lgp30_set_breakpoint(struct LGP30 *machine, uint16_t address);
void lgp30_clear_breakpoint(struct LGP30 *machine, uint16_t address);
bool lgp30_is_breakpoint(const struct LGP30 *machine, uint16_t address);

#endif
