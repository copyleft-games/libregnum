#!/bin/bash
# test-native-unchanged.sh - prove the native build commands did not change
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

# How it works
# ------------
# The native build (`make all`, `make -C tests`, `make -C examples`) must
# print exactly the same commands before and after a build-system change.
# A stored golden file cannot express that portably (the commands embed
# pkg-config output, which differs between Fedora, Ubuntu and aarch64), so
# the baseline is produced on the spot:
#
#   1. hard-link copy the tree (cheap; nothing is written through the links)
#   2. in the copy, put every file that differs from BASE back to its BASE
#      content and delete files BASE does not have -- in the superproject
#      and in every initialised submodule, at the commit BASE records
#   3. run the same `make -Bn` commands in both trees and diff them, with
#      the copy's path rewritten to the real one
#
# `make -n` needs the dependency archives to exist (graylib's raylib rule
# is a shell test), so a native `make all` must have run first.

set -euo pipefail

usage () {
	cat <<'USAGE'
test-native-unchanged.sh - fail when the native build commands differ from
a base git ref.

Usage: test-native-unchanged.sh [-b REF] [-k] [ROOT]

Arguments:
  ROOT          libregnum checkout (default: the parent of this script)

Options:
  -b, --base REF  ref to compare against (default: merge-base with master)
  -k, --keep      keep the scratch copy and the two command logs
  -h, --help      show this help
  --license       show the license

Examples:
  tests/test-native-unchanged.sh
  tests/test-native-unchanged.sh --base v0.11.1
  make test-native-unchanged BASE=master

Exit status: 0 when the commands match, 1 when they differ (a unified
diff is printed), 2 on usage or setup errors.
USAGE
}

license () {
	sed -n '2,16p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'
}

die () {
	printf 'test-native-unchanged: %s\n' "$*" >&2
	exit 2
}

# restore_to_base <real_dir> <copy_dir> <commit>
#
# Make <copy_dir> look like <commit> for every path that differs between
# <commit> and the working tree of <real_dir>: tracked changes, staged
# changes and untracked (non-ignored) files.  Recurses into initialised
# submodules at the commit <commit> records for them.
restore_to_base () {
	if [[ $# -ne 3 ]]
	then
		die 'restore_to_base() requires 3 positional arguments'
	fi

	local real_dir="${1}"
	local copy_dir="${2}"
	local commit="${3}"
	local path
	local sub_path
	local sub_commit

	# changed or deleted tracked files, plus brand-new untracked ones
	while IFS= read -r -d '' path
	do
		# a submodule gitlink shows up as a changed path; handled below
		if [[ -d "${real_dir}/${path}" ]]
		then
			continue
		fi
		rm -f -- "${copy_dir}/${path}"
		if git -C "${real_dir}" cat-file -e "${commit}:${path}" 2>/dev/null
		then
			mkdir -p -- "$(dirname -- "${copy_dir}/${path}")"
			git -C "${real_dir}" show "${commit}:${path}" > "${copy_dir}/${path}"
			# keep the executable bit the base had
			if [[ "$(git -C "${real_dir}" ls-tree "${commit}" -- "${path}" | cut -c1-6)" == '100755' ]]
			then
				chmod +x -- "${copy_dir}/${path}"
			fi
		fi
	done < <(
		git -C "${real_dir}" diff -z --name-only --no-renames "${commit}" --
		git -C "${real_dir}" ls-files -z --others --exclude-standard
	)

	# submodules: compare each against the commit the base records for it
	while read -r sub_path
	do
		if [[ ! -e "${real_dir}/${sub_path}/.git" ]]
		then
			continue
		fi
		sub_commit="$(git -C "${real_dir}" ls-tree "${commit}" -- "${sub_path}" | awk '{print $3}')"
		if [[ -z "${sub_commit}" ]]
		then
			continue
		fi
		if ! git -C "${real_dir}/${sub_path}" cat-file -e "${sub_commit}^{commit}" 2>/dev/null
		then
			die "${sub_path}: base commit ${sub_commit} is not available locally"
		fi
		restore_to_base "${real_dir}/${sub_path}" "${copy_dir}/${sub_path}" "${sub_commit}"
	done < <(git -C "${real_dir}" config --file .gitmodules --get-regexp '\.path$' 2>/dev/null | awk '{print $2}')
}

# dry_run <tree> <log>
#
# Print the native build's commands for the root, tests and examples.
dry_run () {
	if [[ $# -ne 2 ]]
	then
		die 'dry_run() requires 2 positional arguments'
	fi

	local tree="${1}"
	local log="${2}"

	# a caller's MAKEFLAGS (-j, -n, variable overrides) must not leak in
	{
		echo '### make -Bn all'
		env -u MAKEFLAGS -u MFLAGS -u MAKELEVEL make -Bn --no-print-directory -C "${tree}" all 2>&1 || true
		echo '### make -Bn -C tests'
		env -u MAKEFLAGS -u MFLAGS -u MAKELEVEL make -Bn --no-print-directory -C "${tree}/tests" 2>&1 || true
		echo '### make -Bn -C examples'
		env -u MAKEFLAGS -u MFLAGS -u MAKELEVEL make -Bn --no-print-directory -C "${tree}/examples" 2>&1 || true
	} > "${log}"
}

main () {
	local root
	local base=''
	local keep=0
	local base_commit
	local scratch
	local copy
	local entry
	local rc=0

	root="$(CDPATH='' cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"

	while [[ $# -gt 0 ]]
	do
		case "${1}" in
			-b|--base)
				[[ $# -ge 2 ]] || die '--base needs a ref'
				base="${2}"
				shift 2
				;;
			-k|--keep)
				keep=1
				shift
				;;
			-h|--help)
				usage
				exit 0
				;;
			--license)
				license
				exit 0
				;;
			-*)
				usage >&2
				exit 2
				;;
			*)
				root="$(CDPATH='' cd -- "${1}" && pwd)"
				shift
				;;
		esac
	done

	if [[ -z "${base}" ]]
	then
		base="$(git -C "${root}" merge-base HEAD master)" || die 'no merge-base with master; pass --base'
	fi
	base_commit="$(git -C "${root}" rev-parse --verify "${base}^{commit}")" || die "unknown ref: ${base}"

	if [[ ! -f "${root}/deps/graylib/deps/raylib/src/libraylib.a" ]]
	then
		die 'no native dependency build found; run "make all" first'
	fi

	# the scratch copy lives under build/ (ignored, same filesystem for
	# hard links) and never includes itself
	scratch="${root}/build/.native-unchanged"
	rm -rf -- "${scratch}"
	copy="${scratch}/tree"
	mkdir -p -- "${copy}/build"

	# hard-link everything except build/ and trees/ (other worktrees)
	for entry in "${root}"/* "${root}"/.[!.]*
	do
		case "$(basename -- "${entry}")" in
			build|trees|.git)
				continue
				;;
		esac
		cp -al -- "${entry}" "${copy}/"
	done
	# the libregnum outputs the tests/examples rules expect to exist
	for entry in "${root}"/build/release "${root}"/build/debug
	do
		if [[ -d "${entry}" ]]
		then
			cp -al -- "${entry}" "${copy}/build/"
		fi
	done

	restore_to_base "${root}" "${copy}" "${base_commit}"

	dry_run "${copy}" "${scratch}/base.raw"
	dry_run "${root}" "${scratch}/current.log"
	# the base ran in the copy; rewrite its path to the real tree
	sed "s|${copy}|${root}|g" "${scratch}/base.raw" > "${scratch}/base.log"

	if ! diff -u --label "base (${base})" --label 'current' "${scratch}/base.log" "${scratch}/current.log"
	then
		printf '\nFAIL: native build commands differ from %s\n' "${base}" >&2
		rc=1
	else
		printf 'Native build commands unchanged against %s (%s lines)\n' \
			"${base}" "$(wc -l < "${scratch}/current.log")"
	fi

	if [[ "${keep}" -eq 1 ]]
	then
		printf 'kept: %s\n' "${scratch}"
	else
		rm -rf -- "${scratch}"
	fi
	return "${rc}"
}

main "$@"
