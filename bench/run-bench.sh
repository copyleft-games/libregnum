#!/bin/bash
# run-bench.sh - run lrg-bench, under Xvfb when there is no display
#
# Copyright 2026 Zach Podbielniak
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# Usage: run-bench.sh <lrg-bench> [lrg-bench args...]
#   DISPLAY set     -> runs on that display
#   DISPLAY unset   -> starts a private Xvfb (needs Xvfb on PATH)
#   LRG_BENCH_XVFB=1 forces Xvfb even with a display (stable numbers)
set -euo pipefail

run_bench () {
    if [[ $# -lt 1 ]]
    then
        echo 'run-bench.sh requires the lrg-bench binary path'
        echo 'run-bench.sh <lrg-bench> [args...]'
        exit 2
    fi

    local bench_bin="${1}"
    shift

    if [[ -n "${DISPLAY:-}" && "${LRG_BENCH_XVFB:-0}" != "1" ]]
    then
        exec "${bench_bin}" "$@"
    fi

    if ! command -v Xvfb >/dev/null 2>&1
    then
        echo 'run-bench.sh: no DISPLAY and Xvfb is not installed (xorg-x11-server-Xvfb)' >&2
        exit 2
    fi

    # A private, short runtime dir keeps GLFW from picking a Wayland
    # session and keeps the X socket path short.
    local runtime_dir
    runtime_dir="$(mktemp -d /tmp/lrg-bench.XXXXXX)"
    local display_num=$(( 200 + RANDOM % 200 ))
    Xvfb ":${display_num}" -screen 0 640x480x24 -nolisten tcp >/dev/null 2>&1 &
    local xvfb_pid=$!
    trap 'kill "${xvfb_pid}" 2>/dev/null || true; rm -rf "${runtime_dir}"' EXIT
    sleep 1

    local rc=0
    env -u WAYLAND_DISPLAY DISPLAY=":${display_num}" XDG_RUNTIME_DIR="${runtime_dir}" \
        "${bench_bin}" "$@" || rc=$?
    exit "${rc}"
}

run_bench "$@"
