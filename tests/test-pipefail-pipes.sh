#!/bin/bash
# test-pipefail-pipes.sh - no early-exit reader at the end of a pipe in pipefail scripts
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

# Under `set -o pipefail`, `writer | grep -q x` fails at random: grep -q
# exits on the first match, the writer is killed by SIGPIPE writing the rest
# and the pipe's status is the writer's 141.  The same holds for head, grep -m
# and a sed script that quits.  The android-arm64 image build failed this way
# on CI (llvm-readelf -h | grep -q AArch64, about one run in 30).
#
# This test reads every build-system script (all of them: a file without
# pipefail, such as containers/common/common.sh, is sourced by one with it)
# and fails on a pipe into one of those readers, unless the line ends in
# `|| true`.  It
# first checks the detector itself on known good and bad lines.
# docs/cross-compile.org, "The build system itself".

set -euo pipefail

usage () {
	cat <<'USAGE'
test-pipefail-pipes.sh - find pipes into early-exit readers in pipefail scripts.

Usage: test-pipefail-pipes.sh [PROJECT_ROOT]

PROJECT_ROOT defaults to the directory above this script.

Options:
  -h, --help     show this help
  --license      show the license

Examples:
  bash tests/test-pipefail-pipes.sh
  bash tests/test-pipefail-pipes.sh ~/source/projects/libregnum
USAGE
}

# An early-exit reader after a pipe: grep with -q (alone or in a flag group),
# grep -m, head, or sed with a q command.  Comments are ignored.
EARLY_EXIT_RE='\|[[:space:]]*(grep([[:space:]]+-[[:alnum:]]+)*[[:space:]]+-[[:alpha:]]*[qm]|head([[:space:]]|$)|sed[[:space:]][^|]*[0-9;{[:space:]]q)'

# offending_lines FILE: prints "FILE:LINE: TEXT" for each offending line
offending_lines () {
	if [[ $# -ne 1 ]]
	then
		echo "\`offending_lines()\` requires 1 positional argument" >&2
		echo 'offending_lines <file>' >&2
		exit 1
	fi

	local file="${1}"
	local n=0
	local line
	local code

	while IFS= read -r line || [[ -n "${line}" ]]
	do
		n=$((n + 1))
		code="${line%%#*}"
		[[ "${code}" =~ ${EARLY_EXIT_RE} ]] || continue
		[[ "${code}" =~ \|\|[[:space:]]*true[[:space:]]*$ ]] && continue
		printf '%s:%d: %s\n' "${file}" "${n}" "${line}"
	done < "${file}"
}

# check_detector: the pattern finds the bad forms and leaves the good ones
check_detector () {
	local tmp
	local bad=0
	local out

	tmp="$(mktemp)"
	# shellcheck disable=SC2016 # literal script text
	printf '%s\n' \
		'readelf -h f | grep -q AArch64 || die' \
		'x | grep -Fxq -- y' \
		'x | grep -E -q y' \
		'dir="$(tar -tf f | head -n 1)"' \
		'x | grep -m1 y' \
		'x | sed -n "1{p;q}"' \
		> "${tmp}"
	out="$(offending_lines "${tmp}")"
	if [[ "$(wc -l <<< "${out}")" -ne 6 ]]
	then
		echo "FAIL detector: missed a bad line:" >&2
		echo "${out}" >&2
		bad=1
	fi

	# shellcheck disable=SC2016 # literal script text
	printf '%s\n' \
		'[[ "$(readelf -h f)" == *AArch64* ]] || die' \
		'dir="$(tar -tf f | sed -n "1s|/.*||p")"' \
		'x | grep -E y | head -5 >&2 || true' \
		'grep -q y file' \
		'# x | grep -q y in a comment' \
		'x | sed -e s/a/b/ | sort' \
		> "${tmp}"
	out="$(offending_lines "${tmp}")"
	if [[ -n "${out}" ]]
	then
		echo "FAIL detector: flagged a good line:" >&2
		echo "${out}" >&2
		bad=1
	fi
	rm -f -- "${tmp}"
	return "${bad}"
}

main () {
	local root
	local file
	local found=""
	local out
	local failed=0

	case "${1:-}" in
		-h|--help) usage; exit 0 ;;
		--license) echo 'AGPL-3.0-or-later'; exit 0 ;;
	esac
	root="${1:-$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)}"

	check_detector || failed=1

	shopt -s nullglob globstar
	for file in "${root}"/build-aux/**/*.sh "${root}"/build-aux/cross/lrg-*
	do
		[[ -f "${file}" ]] || continue
		out="$(offending_lines "${file}")"
		[[ -n "${out}" ]] && found+="${out}"$'\n'
	done

	if [[ -n "${found}" ]]
	then
		echo "FAIL pipes into an early-exit reader under pipefail:" >&2
		printf '%s' "${found}" >&2
		failed=1
	fi
	if [[ "${failed}" -ne 0 ]]
	then
		exit 1
	fi
	echo 'ok test-pipefail-pipes'
}

main "$@"
