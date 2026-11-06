#!/bin/bash
# docs-check.sh - fail when public API is missing from the org docs
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

set -euo pipefail

usage () {
	cat <<'USAGE'
docs-check.sh - check that every public function in the given headers is
mentioned somewhere under the docs directory.

Usage: docs-check.sh [-d DOCS_DIR] HEADER...

Options:
  -d DIR     documentation root to search (default: docs)
  -h, --help show this help
  --license  show the license

Examples:
  build-aux/docs-check.sh src/paint/*.h src/ui/*.h
  build-aux/docs-check.sh -d docs src/tween/lrg-spring.h

Exit status: 0 when everything is documented, 1 when symbols are
missing (they are listed on stderr), 2 on usage errors.
USAGE
}

main () {
	local docs_dir="docs"
	local -a headers=()

	while [[ $# -gt 0 ]]
	do
		case "${1}" in
			-h|--help)
				usage
				exit 0
				;;
			--license)
				echo 'AGPL-3.0-or-later (see LICENSE)'
				exit 0
				;;
			-d)
				[[ $# -ge 2 ]] || { usage >&2; exit 2; }
				docs_dir="${2}"
				shift 2
				;;
			*)
				headers+=("${1}")
				shift
				;;
		esac
	done

	if [[ ${#headers[@]} -eq 0 ]]
	then
		usage >&2
		exit 2
	fi

	if [[ ! -d "${docs_dir}" ]]
	then
		echo "docs-check: no such directory: ${docs_dir}" >&2
		exit 2
	fi

	# Public functions: the declaration that follows each
	# LRG_AVAILABLE_IN_ALL, skipping G_DECLARE_* type macros and
	# private (_-prefixed) helpers
	local -a symbols=()
	local header
	for header in "${headers[@]}"
	do
		[[ "${header}" == *-private.h ]] && continue
		mapfile -t -O "${#symbols[@]}" symbols < <(
			awk '/LRG_AVAILABLE_IN_ALL/ { want = 1; next }
			     want && /G_DECLARE_/ { want = 0; next }
			     want {
			         if (match ($0, /lrg_[a-z0-9_]+[ \t]*\(/)) {
			             s = substr ($0, RSTART, RLENGTH)
			             sub (/[ \t]*\($/, "", s)
			             print s
			             want = 0
			         }
			     }' "${header}")
	done

	local missing=0
	local symbol
	for symbol in "${symbols[@]}"
	do
		if ! grep -rqF -- "${symbol}" "${docs_dir}"
		then
			echo "undocumented: ${symbol}" >&2
			missing=$((missing + 1))
		fi
	done

	echo "docs-check: ${#symbols[@]} symbols, ${missing} undocumented"
	[[ ${missing} -eq 0 ]]
}

main "$@"
