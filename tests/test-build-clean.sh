#!/bin/bash
# test-build-clean.sh - clean, clean-targets and distclean per build target
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

# The clean goals really delete, so they run in a scratch project: the real
# Makefile, config.mk, rules.mk and build-aux/ symlinked in, a fake build/
# tree with native and cross directories, fake generated headers, and stub
# dependency Makefiles whose `clean' only leaves a marker.  Nothing outside
# the scratch directory is touched.

set -euo pipefail

usage () {
	cat <<'USAGE'
test-build-clean.sh - check what each clean goal removes.

Usage: test-build-clean.sh

Options:
  -h, --help  show this help
  --license   show the license

Examples:
  tests/test-build-clean.sh
  make test-build-system
USAGE
}

case "${1:-}" in
	-h|--help)
		usage
		exit 0
		;;
	--license)
		sed -n '2,16p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'
		exit 0
		;;
esac

BS_NAME='test-build-clean'
# shellcheck source=tests/build-system-lib.sh
source "$(dirname -- "${BASH_SOURCE[0]}")/build-system-lib.sh"

SCRATCH="$(mktemp -d "${TMPDIR:-/tmp}/lrg-clean-test.XXXXXX")"
trap 'rm -rf -- "${SCRATCH}"' EXIT

NATIVE_DIRS='build/release build/debug build/sanitize build/.native-unchanged'
CROSS_DIRS='build/release-windows-amd64 build/debug-windows-amd64 build/release-arm64 build/debug-arm64 build/release-android-arm64'

# populate -- (re)create the scratch project's fake outputs
populate () {
	local dir
	local sub

	for dir in ${NATIVE_DIRS} ${CROSS_DIRS}
	do
		mkdir -p -- "${SCRATCH}/${dir}/obj"
		: > "${SCRATCH}/${dir}/obj/marker.o"
	done
	mkdir -p -- "${SCRATCH}/src"
	: > "${SCRATCH}/src/lrg-version.h"
	: > "${SCRATCH}/src/config.h"
	for sub in graylib yaml-glib
	do
		mkdir -p -- "${SCRATCH}/deps/${sub}"
		printf 'clean:\n\t@: > %s/%s.cleaned\n' "${SCRATCH}" "${sub}" > "${SCRATCH}/deps/${sub}/Makefile"
		rm -f -- "${SCRATCH}/${sub}.cleaned"
	done
}

# present -- the fake directories that still exist, space-separated
present () {
	local dir
	local out=''

	for dir in ${NATIVE_DIRS} ${CROSS_DIRS}
	do
		if [[ -d "${SCRATCH}/${dir}" ]]
		then
			out+="${dir} "
		fi
	done
	printf '%s' "${out% }"
}

scratch_make () {
	bs_make -s -C "${SCRATCH}" -f Makefile LRG_HOST_TARGET=linux-amd64 PKG_CONFIG=: "$@" > /dev/null 2>&1
}

for file in Makefile config.mk rules.mk build-aux
do
	ln -s -- "${BS_ROOT}/${file}" "${SCRATCH}/${file}"
done

echo '== native clean'
populate
scratch_make clean
bs_eq 'native clean keeps only the cross dirs' "${CROSS_DIRS}" "$(present)"
bs_eq 'native clean removes lrg-version.h' no "$([[ -e "${SCRATCH}/src/lrg-version.h" ]] && echo yes || echo no)"
bs_eq 'native clean removes config.h' no "$([[ -e "${SCRATCH}/src/config.h" ]] && echo yes || echo no)"

echo '== cross clean'
populate
scratch_make WINDOWS=1 clean
bs_eq 'WINDOWS=1 clean removes only windows-amd64' \
	"${NATIVE_DIRS} build/release-arm64 build/debug-arm64 build/release-android-arm64" "$(present)"
bs_eq 'cross clean keeps the shared headers' yes "$([[ -e "${SCRATCH}/src/config.h" ]] && echo yes || echo no)"
populate
scratch_make ARM64=1 DEBUG=1 clean
bs_eq 'ARM64=1 clean removes release-arm64 and debug-arm64' \
	"${NATIVE_DIRS} build/release-windows-amd64 build/debug-windows-amd64 build/release-android-arm64" "$(present)"

echo '== clean-targets'
populate
scratch_make clean-targets
bs_eq 'clean-targets removes every cross dir' "${NATIVE_DIRS}" "$(present)"

echo '== distclean'
populate
scratch_make distclean
bs_eq 'distclean removes build/' no "$([[ -e "${SCRATCH}/build" ]] && echo yes || echo no)"
bs_eq 'distclean cleans graylib' yes "$([[ -e "${SCRATCH}/graylib.cleaned" ]] && echo yes || echo no)"
bs_eq 'distclean cleans yaml-glib' yes "$([[ -e "${SCRATCH}/yaml-glib.cleaned" ]] && echo yes || echo no)"

bs_finish
