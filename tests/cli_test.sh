#!/usr/bin/env bash
# Exercises the real bin/lgp30 binary end-to-end (argument parsing, file
# loading, real stdin/stdout, exit codes) -- coverage the library-level
# tests (tests/test_bootstrap.c etc.) don't reach since they call
# lgp30_step() directly. Uses the same 1957 bootstrap-loader program (also
# cataloged as "Program 09.0" in the Subroutine Manual) already verified
# bit-for-bit at the library level.
set -u
BIN="${1:?usage: cli_test.sh <path-to-lgp30-binary>}"
FIXTURES="$(dirname "$0")/fixtures"
fail=0

check() {
    local desc="$1"
    local got="$2"
    local want="$3"
    if [ "$got" != "$want" ]; then
        echo "FAIL: $desc (got [$got], want [$want])"
        fail=1
    else
        echo "PASS: $desc"
    fi
}

# Bootstrap program halts cleanly (exit 0) once real stdin is exhausted.
printf '' | "$BIN" "$FIXTURES/bootstrap.asm" >/tmp/lgp30_cli_out.$$ 2>/tmp/lgp30_cli_err.$$
check "bootstrap program exits 0 on empty tape" "$?" "0"

# Flexowriter mode: prints "hi" via the real code table.
out="$("$BIN" --io=flexowriter "$FIXTURES/print_flexo.asm" </dev/null)"
check "flexowriter mode prints 'hi'" "$out" "hi"

# Simple mode: raw byte 33 passthrough, not a Flexowriter glyph.
byte="$("$BIN" --io=simple "$FIXTURES/print_simple.asm" </dev/null | od -An -tu1 | tr -d ' ')"
check "simple mode passes byte 33 through raw" "$byte" "33"

# A malformed program reports a structured error and exits 1.
echo "not valid" > /tmp/lgp30_cli_bad.$$.asm
"$BIN" "/tmp/lgp30_cli_bad.$$.asm" >/dev/null 2>/tmp/lgp30_cli_bad_err.$$
check "malformed program exits 1" "$?" "1"
check "error message cites line:column" \
    "$(grep -c ':1:1:' /tmp/lgp30_cli_bad_err.$$)" "1"

# A missing file exits 2.
"$BIN" /tmp/lgp30_cli_does_not_exist.asm >/dev/null 2>&1
check "missing file exits 2" "$?" "2"

rm -f /tmp/lgp30_cli_out.$$ /tmp/lgp30_cli_err.$$ /tmp/lgp30_cli_bad.$$.asm /tmp/lgp30_cli_bad_err.$$

exit $fail
