# LGP-30 Emulator

## Overview

This document covers the emulator's architecture: module layout, the machine's bit
layout and timing model, and every opcode's semantics with manual page citations. See
[README.md](README.md) for build and usage instructions.

The emulator models drum rotation timing and its effect on instruction placement, not
just opcode semantics — this is not just a calculator with unfamiliar opcodes.

## Architecture

The core (`core/`) is a self-contained library with no host attached to it — the CLI
(`host/cli/`) is one consumer of it. The split is deliberate: the core is designed so
alternate front-ends could reuse it without modification.

**Step-driven, not loop-driven.** `lgp30_step(machine)` advances one instruction and
returns status. The host drives the loop at its own pace; the core never blocks or
assumes a particular loop.

**No global state.** Everything lives in a `struct LGP30` that is heap-allocated. Each
host instantiates one, and tests instantiate as many as needed.

**No platform headers in core.** `<stdio.h>` only in the host layer. Core is pure logic.

**I/O abstraction.** The core never calls I/O directly — only through an `io_ops`
struct of function pointers (read char, write char, flush). Two implementations:
- **Accurate mode**: Flexowriter character mapping + paper tape timing
- **Simplified mode**: raw ASCII passthrough

**Assembler in core, not in CLI.** Any host that needs to assemble a program links
against the same code the CLI does.

**Structured assembler errors.** Line number + column + message — not bare
`fprintf(stderr, ...)` — so a host can surface errors inline rather than just dumping
them to a stream.

## Module Layout

```
core/
  types.h         — word32, machine struct, bit layout, number representation, address notation
  drum.c/h        — drum timing, read/write by address
  cpu.c/h         — fetch/decode/execute, step(), inspection API (registers/memory/breakpoints)
  io.h            — io_ops interface (function pointers)
  asm/
    asm.h/c       — assembler (raw addresses only, no labels, in v1)
    image.h/c     — memory image serialization (assembler output -> CLI/REPL input)
io/
  flexowriter.c   — accurate mode: real Flexowriter code table + shift state
  simple.c        — simplified mode: raw byte passthrough
host/
  cli/
    main.c        — batch mode: assemble, run, report, exit code
    repl.c         — interactive debugger: step/run/break/dump/registers/reset/load
    program_loader.h/c — shared "read file, assemble, load into machine" (main.c + repl.c)
    display.h/c    — shared register/word formatting (main.c + repl.c)
```

The REPL is the forcing function for the inspection API: it needs read-only access to
registers/memory and breakpoints, kept general enough that another front-end could
reuse it.

## The Machine

**Drum**: 3700 RPM (17 ms/revolution) — the manual states this consistently in two
places — 64 tracks, 64 sectors per revolution (~260 µs per sector). Every memory
access — instruction fetch and operand — waits for the right sector to pass under the
read head. Instruction placement on the drum is the optimisation problem.

**Word size**: 32 bits

**Memory**: 4096 words (64 tracks × 64 sectors)

**Hex character set**: LGP-30 uses non-standard digits — `f g j k q w` instead of
`A B C D E F`. Distinct from, and not to be confused with, opcode mnemonic letters
(which happen to reuse some of the same letters for different values) or the
Flexowriter's separate 6-bit I/O code table. In v1 this alphabet is not used for
address notation (see below) — it's reserved for representing raw word contents by
hand (e.g. a hex word-dump), which nothing in v1 currently wires up.

**Address notation**: 4 *decimal* digits; first 2 = track number (00–63), last 2 =
sector number (00–63) — e.g. `2000` = track 20, sector 0; `2710` = track 27, sector 10.
Confirmed against the manual's own worked examples: `p 2000`'s instruction decodes to
track-bits `010100` = 20 decimal, not 32 = 0x20; and the manual states the valid
bootstrap-fill address range as "0008 through 6363", which only makes sense if track
and sector are each independently 00–63 in decimal — a hex-pair reading would cap out
at "3f3f". The LGP-30 hex alphabet above is a *separate* notation, used only for
writing raw word contents by hand for bootstrap/Flexowriter entry — e.g. the manual
shows `c 2710` as raw hex `c1g28` — which v1 does not implement.

### Word and Instruction Bit Layout

Confirmed from the manual's bit diagram (p.10 of the manual, 1-indexed MSB-first there). Translated to 0-indexed LSB-first for a `uint32_t`:

```
bit 31       = sign (0 = positive, 1 = negative)
bits 30..21  = high-order magnitude / unused for instructions
bits 20..17  = opcode           (4 bits)   opcode  = (word >> 17) & 0xF
bits 16..15  = unused (2 bits)
bits 14..9   = track            (6 bits)   track   = (word >> 9)  & 0x3F
bits 8..3    = sector           (6 bits)   sector  = (word >> 3)  & 0x3F
bits 2..1    = unused (2 bits)
bit 0        = spacer — always 0 on the drum; may transiently be 1 in the
               accumulator after a 4-bit input shift. `drum_write()` must
               mask this bit to 0; the accumulator itself must not.

address = (word >> 3) & 0xFFF   // track (upper 6 bits) + sector (lower 6), contiguous
```

### Instruction Set

| Letter | Opcode (bin) | Opcode (hex) | Name |
|--------|--------------|--------------|------|
| z | 0000 | 0 | Stop |
| b | 0001 | 1 | Bring |
| y | 0010 | 2 | Store address |
| r | 0011 | 3 | Return address |
| i | 0100 | 4 | Input |
| d | 0101 | 5 | Divide |
| n | 0110 | 6 | N Multiply |
| m | 0111 | 7 | M Multiply |
| p | 1000 | 8 | Print |
| e | 1001 | 9 | Extract |
| u | 1010 | f | Unconditional transfer |
| t | 1011 | g | Test (branch if negative) |
| h | 1100 | j | Hold and Store |
| c | 1101 | k | Clear and Store |
| a | 1110 | q | Add |
| s | 1111 | w | Subtract |

All 16 opcodes are now confirmed (cross-validated against both the manual's Summary of
Orders table and its Flexowriter Commands code table, which independently agree), and
all 16 semantics are confirmed against the manual's own prose and worked examples
(pp.15-29) — implemented and golden-vector-tested in `core/cpu.c`.

**Negative number representation**: two's complement of the 31-bit sign+magnitude field
(word bits 31..1; bit 0 is the spacer), NOT plain sign-magnitude. Confirmed directly from
the manual's own example (p.27): "6 at q=4" is `0|011000...0`; "-6 at q=4" is
`1|101000...0`, which is `~(011000...0)+1`, not the same magnitude bits with only the
sign flipped.

- **Stop (`z`)**: halts computation.
- **Bring (`b`)**: `acc = mem[addr]`.
- **Store address (`y`)**: replaces only the address-field bits (word bits 14..3) of
  `mem[addr]` with the accumulator's address-field bits; `mem[addr]`'s opcode bits and
  the accumulator are both otherwise unaffected. The classic self-modifying-address idiom
  (e.g. the bootstrap program's own loop, p.35).
- **Return address (`r`)**: replaces only the address-field bits of `mem[addr]` with
  `(address of this r instruction) + 2` — one past the transfer instruction
  conventionally placed immediately after `r`, so execution resumes just past that
  transfer once the callee jumps back. `mem[addr]`'s opcode bits and the accumulator are
  otherwise unaffected. Confirmed via the manual's own example (p.18): `r 3050` located
  at 1013 stores return address 1015 into location 3050.
- **Input (`i`)**: only ever used as `i 0000`, always preceded by `p 0000`. A *single*
  `i 0000` autonomously shifts a 4-bit code into the accumulator's low bits (pushing
  existing content left) once per character, repeating until either the 8th character
  (the accumulator is 32 bits) or a stop code (`100000`=32, "Cond Stop" on the keyboard
  code table) is read — it does **not** take one `i 0000` per character. Confirmed
  directly: the bootstrap program (p.35) issues exactly one `i 0000` per word read. In
  `cpu.c` this spans multiple `lgp30_step()` calls (one character per call, via
  `input_in_progress`/`op_input_continue`) rather than blocking in a loop, preserving the
  step-driven contract.
- **Divide (`d`)**: `acc = acc / mem[addr]` on true (sign, magnitude) values (not the raw
  two's-complement bit pattern), quotient **rounded** (not truncated) to 30 bits.
  Overflows and halts the machine if the quotient's magnitude would be ≥ 1.
- **N Multiply (`n`)**: `acc = acc * mem[addr]` on magnitudes, keeps the **low 31 bits**
  of the up-to-60-bit magnitude product, placed directly into the accumulator's
  sign+magnitude bits. The result has **no true sign bit** — that slot holds a magnitude
  bit instead of a real sign (the manual is explicit: "the sign position in this case
  represents magnitude and not sign"). No overflow possible. Used for left-shifting.
- **M Multiply (`m`)**: `acc = acc * mem[addr]` on magnitudes, keeps the **top 30 bits**
  of the product plus a sign from XOR-ing the two operand signs ("fractional"/
  right-shift interpretation). Truncates, does not round — confirmed by the manual's own
  truncation example (p.26). No overflow possible (product of two fractions in [0,1)
  can never reach 1).
- **Print (`p`)**: the address's **track field** (6 bits) is the literal Flexowriter
  output code, not a memory address — confirmed directly (p.33): `p 2000` (decimal track
  20) has track-bits `010100` = 20, "which is the code for a back space." Print never
  touches memory, the accumulator, or the counter.
- **Extract (`e`)**: `acc = acc & mem[addr]` — bitwise AND with a mask word; `mem[addr]`
  is unaffected.
- **Unconditional transfer (`u`)**: `counter = addr`; does not affect the accumulator.
- **Test (`t`)**: branches (acts as unconditional transfer) if the accumulator's sign bit
  is 1 (negative); otherwise falls through to the next instruction.
- **Hold and Store (`h`)**: `mem[addr] = acc`; accumulator unaffected ("hold" its value).
- **Clear and Store (`c`)**: `mem[addr] = acc`, then `acc = 0`. Confirmed directly (p.18).
- **Add (`a`)** / **Subtract (`s`)**: signed add/subtract on the true (two's-complement)
  values. Overflows and halts if the result falls outside `[-2^30, 2^30-1]`.

### I/O Protocol

- `p 0000` starts the tape reader; must be followed (not necessarily immediately) by `i 0000`
- `i 0000` shifts 4-bit Flexowriter codes into the accumulator as characters are read
- A stop code (`100000`) on tape halts the reader and restarts the CPU
- Printing a character takes ~6 drum revolutions; a stop instruction should follow each print
- `MANUAL` switch on Flexowriter redirects `p 0000 / i 0000` to keyboard entry

### Timing Constants

From the manual's specifications table: access time 2–17 ms, transfer time 1–17 ms,
addition ≈0.26 ms (excluding access — about one sector-length), multiply/divide ≈17 ms
(excluding access — about one full extra revolution), 120 kHz bit clock (self-consistent:
32 bits / 120 kHz ≈ 266 µs ≈ one sector). Register-only arithmetic (not touching main
memory) does not pay drum rotational-wait time — only `drum_read`/`drum_write` do.

## Manuals

[bitsavers.org](https://bitsavers.org/pdf/royalPrecision/LGP-30/)

## Licence

MIT — see `LICENSE`.
