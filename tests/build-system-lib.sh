#!/bin/bash
# build-system-lib.sh - helpers shared by the build-system tests
# Copyright (C) 2026  Zach Podbielniak
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU Affero General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU Affero General Public License for more details.
#
# You should have received a copy of the GNU Affero General Public License
# along with this program.  If not, see <https://www.gnu.org/licenses/>.

# Sourced, never run.  Provides:
#
#   bs_make ARGS...            run make in $BS_ROOT with no inherited
#                              MAKEFLAGS (a caller's -j/-n must not leak)
#   bs_vars VARS -- ARGS...    print VAR=value for each of the
#                              space-separated VARS, as the root Makefile
#                              resolves them with the make ARGS given
#   bs_var VAR ARGS...         print just VAR's value
#   bs_eq NAME EXPECTED ACTUAL record a pass or a failure
#   bs_contains NAME NEEDLE HAYSTACK
#   bs_not_contains NAME NEEDLE HAYSTACK
#   bs_fails NAME PATTERN ARGS...  make ARGS must fail and print PATTERN
#   bs_finish                  print the summary, exit 1 on any failure
#
# Cross-target configurations are resolved on hosts without the target's
# toolchain, so bs_vars passes PKG_CONFIG=: (a no-op) unless the caller
# sets one; nothing here compiles.

BS_ROOT="${BS_ROOT:-$(CDPATH='' cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)}"
BS_PASS=0
BS_FAIL=0
BS_NAME="${BS_NAME:-build-system}"

# An audit makefile read after the real one: prints the requested variables
# (make syntax, so nothing in it may expand in the shell)
# shellcheck disable=SC2016
BS_AUDIT_MK='
.PHONY: lrg-audit-vars
lrg-audit-vars:
	@$(foreach v,$(LRG_AUDIT_VARS),printf "%s=%s\n" "$(v)" "$($(v))";)
'

bs_make () {
	env -u MAKEFLAGS -u MFLAGS -u MAKELEVEL make --no-print-directory "$@"
}

bs_vars () {
	if [[ $# -lt 2 ]]
	then
		echo 'bs_vars() requires VARS -- ARGS...' >&2
		return 2
	fi

	local vars="${1}"
	shift
	if [[ "${1:-}" == '--' ]]
	then
		shift
	fi

	printf '%s' "${BS_AUDIT_MK}" | bs_make -s -C "${BS_ROOT}" -f Makefile -f - \
		lrg-audit-vars LRG_AUDIT_VARS="${vars}" PKG_CONFIG=: "$@" 2>/dev/null
}

bs_var () {
	if [[ $# -lt 1 ]]
	then
		echo 'bs_var() requires VAR [ARGS...]' >&2
		return 2
	fi

	local var="${1}"
	shift
	bs_vars "${var}" -- "$@" | sed -n "s/^${var}=//p"
}

bs_pass () {
	BS_PASS=$((BS_PASS + 1))
	printf '  ok    %s\n' "${1}"
}

bs_failed () {
	BS_FAIL=$((BS_FAIL + 1))
	printf '  FAIL  %s\n' "${1}" >&2
	shift
	if [[ $# -gt 0 ]]
	then
		printf '        %s\n' "$@" >&2
	fi
}

bs_eq () {
	if [[ $# -ne 3 ]]
	then
		echo 'bs_eq() requires NAME EXPECTED ACTUAL' >&2
		return 2
	fi

	if [[ "${2}" == "${3}" ]]
	then
		bs_pass "${1}"
	else
		bs_failed "${1}" "expected: '${2}'" "actual:   '${3}'"
	fi
}

bs_contains () {
	if [[ $# -ne 3 ]]
	then
		echo 'bs_contains() requires NAME NEEDLE HAYSTACK' >&2
		return 2
	fi

	if [[ "${3}" == *"${2}"* ]]
	then
		bs_pass "${1}"
	else
		bs_failed "${1}" "expected to find: '${2}'" "in: '${3}'"
	fi
}

bs_not_contains () {
	if [[ $# -ne 3 ]]
	then
		echo 'bs_not_contains() requires NAME NEEDLE HAYSTACK' >&2
		return 2
	fi

	if [[ "${3}" != *"${2}"* ]]
	then
		bs_pass "${1}"
	else
		bs_failed "${1}" "did not expect: '${2}'" "in: '${3}'"
	fi
}

# bs_fails NAME PATTERN ARGS... -- make ARGS (audit goal) must exit non-zero
# and its output must contain PATTERN
bs_fails () {
	if [[ $# -lt 3 ]]
	then
		echo 'bs_fails() requires NAME PATTERN ARGS...' >&2
		return 2
	fi

	local name="${1}"
	local pattern="${2}"
	local out
	local rc=0
	shift 2

	out="$(printf '%s' "${BS_AUDIT_MK}" | bs_make -s -C "${BS_ROOT}" -f Makefile -f - \
		lrg-audit-vars LRG_AUDIT_VARS=BUILDDIR "$@" 2>&1)" || rc=$?
	if [[ "${rc}" -eq 0 ]]
	then
		bs_failed "${name}" "expected make to fail, it succeeded: ${out}"
	elif [[ "${out}" != *"${pattern}"* ]]
	then
		bs_failed "${name}" "expected the error to mention: '${pattern}'" "got: ${out}"
	else
		bs_pass "${name}"
	fi
}

bs_finish () {
	printf '%s: %d passed, %d failed\n' "${BS_NAME}" "${BS_PASS}" "${BS_FAIL}"
	if [[ "${BS_FAIL}" -ne 0 ]]
	then
		exit 1
	fi
}
