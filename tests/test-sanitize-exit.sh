#!/bin/sh
# Copyright 2026 Zach Podbielniak
# SPDX-License-Identifier: AGPL-3.0-or-later

# Exercise the real runner with a program that reports recoverable UB.
set -eu
test_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
probe_dir=$(mktemp -d)
trap 'rm -rf "$probe_dir"' EXIT HUP INT TERM
cat > "$probe_dir/probe.c" <<'EOF'
#include <limits.h>
int main (void)
{
    volatile int value = INT_MAX;
    volatile int overflow = value + 1;
    (void)overflow;
    return 0;
}
EOF
${CC:-cc} -fsanitize=undefined -o "$probe_dir/probe" "$probe_dir/probe.c"

# The positive control proves that UBSan catches this fixture.
if UBSAN_OPTIONS=halt_on_error=1 "$probe_dir/probe" > "$probe_dir/control.log" 2>&1; then
    cat "$probe_dir/control.log"
    printf 'UBSan failed to detect the positive control\n' >&2
    exit 1
fi
if MAKEFLAGS= MFLAGS= make --no-print-directory -s -C "$test_dir" run-sanitize \
    SANITIZER_EXIT_CHECK=0 TEST_SOURCES= SANITIZE_BINARIES="$probe_dir/probe" \
    BUILD_GIR=0 MCP=0 CAD=0 FFMPEG=0 > "$probe_dir/runner.log" 2>&1; then
    cat "$probe_dir/runner.log"
    printf 'Sanitizer runner incorrectly accepted undefined behavior\n' >&2
    exit 1
fi
if ! grep -q 'runtime error: signed integer overflow' "$probe_dir/runner.log"; then
    cat "$probe_dir/runner.log"
    printf 'Sanitizer runner failed without exercising the UB fixture\n' >&2
    exit 1
fi
printf 'Sanitizer exit regression passed\n'
