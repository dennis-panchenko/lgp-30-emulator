#!/usr/bin/env bash
# Exercises the real `bin/lgp30 repl` interactive loop end-to-end via
# piped commands.
set -u
BIN="${1:?usage: repl_test.sh <path-to-lgp30-binary>}"
FIXTURES="$(dirname "$0")/fixtures"
fail=0

check_contains() {
    local desc="$1"
    local haystack="$2"
    local needle="$3"
    if echo "$haystack" | grep -qF "$needle"; then
        echo "PASS: $desc"
    else
        echo "FAIL: $desc (expected to find [$needle])"
        fail=1
    fi
}

# repl_demo.asm: "a 0002" (adds dw 5 = word 0x0000000f) then "z 0000".
out="$(printf 'step\nregisters\nquit\n' | "$BIN" repl "$FIXTURES/repl_demo.asm")"
check_contains "step executes one instruction and advances the counter" "$out" "counter now 0001"
check_contains "registers reflects the add" "$out" "accumulator:          0000000f"

# A breakpoint stops `run` BEFORE executing the breakpointed instruction.
out="$(printf 'break 0001\nrun\nregisters\nquit\n' | "$BIN" repl "$FIXTURES/repl_demo.asm")"
check_contains "run stops at the breakpoint" "$out" "breakpoint hit at 0001"
check_contains "status is still running, not stopped, at the breakpoint" "$out" "status:               running"

# Without a breakpoint, run executes to the z instruction and halts.
out="$(printf 'run\nregisters\nquit\n' | "$BIN" repl "$FIXTURES/repl_demo.asm")"
check_contains "run halts at the z instruction" "$out" "halted: stopped (z instruction)"
check_contains "status reflects the halt" "$out" "status:               stopped (z instruction)"

# dump shows the data word regardless of whether it was ever fetched as
# an instruction.
out="$(printf 'dump 0002 1\nquit\n' | "$BIN" repl "$FIXTURES/repl_demo.asm")"
check_contains "dump shows the data word at 0002" "$out" "0002: 0000000f"

# reset clears execution state but keeps memory (and breakpoints).
out="$(printf 'run\nreset\nregisters\ndump 0002 1\nquit\n' | "$BIN" repl "$FIXTURES/repl_demo.asm")"
check_contains "reset rewinds the counter" "$out" "counter:              0000"
check_contains "reset rewinds the accumulator" "$out" "accumulator:          00000000"
check_contains "reset does not touch loaded memory" "$out" "0002: 0000000f"

# An unrecognized command doesn't crash the loop.
out="$(printf 'bogus\nregisters\nquit\n' | "$BIN" repl "$FIXTURES/repl_demo.asm")"
check_contains "unknown command reports an error, not a crash" "$out" "unknown command 'bogus'"
check_contains "the loop continues afterward" "$out" "status:               running"

exit $fail
