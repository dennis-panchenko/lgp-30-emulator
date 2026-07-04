# LGP-30 Emulator

A from-scratch emulator of the Royal McBee LGP-30 (1956) — a magnetic-drum
computer with 4096 words of memory and no cache, where every instruction fetch
waits for the right sector to rotate under the read head. Instruction
placement on the drum *is* the optimization problem; the LGP-30 is the
machine from Ed Nather's [Story of Mel](http://www.catb.org/jargon/html/story-of-mel.html)
— it's the drum-timing tricks Mel Kaye is famous for. This project tries to
capture that feel, not just emulate the opcodes.

Written in C, step-driven (no blocking run loop) so the same core doesn't
assume any particular host's event loop.

## Status

v1 is done: the full CPU (all 16 opcodes), drum rotational timing, a small
assembler, a batch CLI, and an interactive REPL debugger. Golden-vector
tested against the 1957 Programming Manual's own worked examples, including
the manual's bootstrap-loader program reproduced bit-for-bit.

## Building

Requires a C11 compiler and `make`. No other dependencies.

```
make            # builds bin/lgp30
make test       # builds and runs the full test suite
make debug      # debug build with AddressSanitizer/UBSan
```

## Usage

Batch mode — assemble and run a program, non-interactively:

```
./bin/lgp30 --verbose program.asm
```

Interactive REPL — step through a program, inspect registers/memory, set
breakpoints:

```
./bin/lgp30 repl program.asm
lgp30> step
lgp30> registers
lgp30> break 0010
lgp30> run
lgp30> dump 0000 8
```

Run `./bin/lgp30 --help` or `help` inside the REPL for the full command list.

## A tiny example

Assembly source is `<location> <opcode> <address>` per line — both the
location and address are 4-digit *decimal* (track, sector), not hex. This
adds 5 and 3 and stops:

```
start 0000
0000 b 0003  ; bring 5 into the accumulator
0001 a 0004  ; add 3
0002 z 0000  ; stop
0003 dw 5
0004 dw 3
```

```
$ ./bin/lgp30 --verbose add.asm
steps: 3
drum time: 34.531 ms
halt reason: stopped (z instruction)
accumulator: 00000010
counter: 0003
```

(`00000010` is the LGP-30's own hex alphabet — `0-9 f g j k q w` — for the
raw word `8<<1`; see [ABOUT.md](ABOUT.md) for why the low bit is a spacer.)

## How it works

[ABOUT.md](ABOUT.md) has the real architecture writeup: word/instruction bit
layout, drum timing model, every opcode's semantics with manual citations,
and the module layout. That's the doc to read before touching `core/`.

## Manuals

[bitsavers.org](https://bitsavers.org/pdf/royalPrecision/LGP-30/)

## Contributing

Issues and PRs welcome, especially corrections — this was built by an
enthusiast reading 1957 documentation, not a subject-matter expert, so
mistakes are entirely possible. If you spot one, a PR with a manual page
citation is the fastest way to get it fixed.

## Authors

Dennis Panchenko, with Claude (Anthropic) as coding assistant and co-author.

## License

MIT — see [LICENSE](LICENSE).
