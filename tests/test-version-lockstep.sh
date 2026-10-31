#!/bin/bash
# Copyright 2026 Zach Podbielniak
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# test-version-lockstep.sh - libregnum and the bundled graylib must share
# MAJOR.MINOR (AGENTS.md, "Versioning"); only the micro versions may differ.
#
# Usage: test-version-lockstep.sh [-h|--help] [--license] [PROJECT_ROOT]
#   test-version-lockstep.sh            check the checkout this script is in
#   test-version-lockstep.sh ../other   check another checkout
# GRAYLIB_DIR selects the dependency used by the build instead of deps/graylib.
set -euo pipefail

# read_version <config.mk> <MAJOR|MINOR|MICRO>: prints that field
read_version () {
    if [[ $# -ne 2 ]]
    then
        echo 'read_version() requires 2 positional arguments' >&2
        echo 'read_version <config.mk> <MAJOR|MINOR|MICRO>' >&2
        exit 2
    fi

    local config_mk="${1}"
    local field="${2}"
    local value

    value="$(sed -n "s/^VERSION_${field}[[:space:]]*:=[[:space:]]*\([0-9][0-9]*\)[[:space:]]*$/\1/p" "${config_mk}")"
    if [[ -z "${value}" ]]
    then
        echo "test-version-lockstep: no VERSION_${field} in ${config_mk}" >&2
        exit 2
    fi
    echo "${value}"
}

check_lockstep () {
    local root="${1}"
    local lrg_mk="${root}/config.mk"
	# Match config.mk's explicit dependency selection without moving gitlinks.
	local grl_mk="${GRAYLIB_DIR:-${root}/deps/graylib}/config.mk"
    local lrg_major lrg_minor lrg_micro grl_major grl_minor grl_micro

    if [[ ! -f "${lrg_mk}" || ! -f "${grl_mk}" ]]
    then
        echo "test-version-lockstep: ${lrg_mk} or ${grl_mk} missing (submodules initialised?)" >&2
        exit 2
    fi

    lrg_major="$(read_version "${lrg_mk}" MAJOR)"
    lrg_minor="$(read_version "${lrg_mk}" MINOR)"
    lrg_micro="$(read_version "${lrg_mk}" MICRO)"
    grl_major="$(read_version "${grl_mk}" MAJOR)"
    grl_minor="$(read_version "${grl_mk}" MINOR)"
    grl_micro="$(read_version "${grl_mk}" MICRO)"

    if [[ "${lrg_major}.${lrg_minor}" != "${grl_major}.${grl_minor}" ]]
    then
        echo "test-version-lockstep: libregnum ${lrg_major}.${lrg_minor}.${lrg_micro} and graylib" \
             "${grl_major}.${grl_minor}.${grl_micro} differ in MAJOR.MINOR; bump graylib to" \
             "${lrg_major}.${lrg_minor}.0 (or libregnum to ${grl_major}.${grl_minor}.0) together" >&2
        exit 1
    fi

    echo "version lockstep: libregnum ${lrg_major}.${lrg_minor}.${lrg_micro}, graylib ${grl_major}.${grl_minor}.${grl_micro}"
}

main () {
    local script_dir
    local root

    case "${1:-}" in
        -h|--help)
            sed -n '4,11p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'
            exit 0
            ;;
        --license)
            echo 'test-version-lockstep.sh  Copyright (C) 2026  Zach Podbielniak'
            echo 'License AGPLv3+: GNU AGPL version 3 or later <https://www.gnu.org/licenses/agpl.html>'
            exit 0
            ;;
    esac

    script_dir="$(CDPATH='' cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
    root="${1:-${script_dir}/..}"
    check_lockstep "${root}"
}

main "$@"
