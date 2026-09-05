#!/bin/sh
# Copyright 2026 Zach Podbielniak
# SPDX-License-Identifier: AGPL-3.0-or-later

# Inspect the actual test selection without compiling optional backends.
set -eu
test_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)

check_config()
{
    sources=$(MAKEFLAGS= MFLAGS= make --no-print-directory -s -C "$test_dir" \
        -f Makefile -f - audit-test-sources \
        HAS_LUAJIT="$1" HAS_PYTHON="$2" HAS_GI="$3" HAS_GJS="$4" \
        MCP=0 CAD=0 FFMPEG=0 <<'EOF'
.PHONY: audit-test-sources
audit-test-sources:
	@printf '%s\n' $(TEST_SOURCES)
EOF
    )

    for entry in "test-scripting.c:$1" "test-scripting-python.c:$2" \
                 "test-scripting-pygobject.c:$2" "test-scripting-gi.c:$3" \
                 "test-scripting-gjs.c:$4"; do
        source=${entry%:*}
        expected=${entry#*:}
        present=0
        for selected in $sources; do
            if [ "$selected" = "$source" ]; then
                present=1
            fi
        done
        if [ "$present" != "$expected" ]; then
            printf 'Incorrect selection: %s present=%s expected=%s (backends: %s)\n' \
                "$source" "$present" "$expected" "$*" >&2
            exit 1
        fi
    done
}

check_config 0 0 0 0
check_config 1 0 0 0
check_config 0 1 1 0
check_config 0 0 1 1
check_config 1 1 1 1
printf 'Build configuration regression tests passed\n'
