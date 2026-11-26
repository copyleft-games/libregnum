#!/bin/bash
# test-cross-artifacts.sh - check what a cross build produced
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

# Checks the binaries of one target's build directory, the part of a cross
# build its tests cannot see (`make test-cross-<target>' runs it in the
# target's build container, after the build):
#
#   linux-arm64     every shared library, program and static archive
#                   member (libregnum, graylib) is AArch64
#   windows-amd64   libregnum.dll, graylib.dll and every .exe are PE32+
#                   x86-64; libregnum.dll exports lrg_*, graylib.dll exports
#                   grl_* and raylib's functions; every DLL libregnum.dll
#                   imports is found next to it, in the MinGW runtime or
#                   among Windows' own DLLs (Wine's copies)
#   android-arm64   liblibregnum.a's members and the shared libraries are
#                   AArch64, and every shared library's LOAD segments are
#                   16 KB aligned (Android 15+ devices with 16 KB pages)
#
# Exit status: 0 when every check passes, 1 otherwise, 2 on bad usage.

set -euo pipefail

usage () {
	cat <<'USAGE'
test-cross-artifacts.sh - check the binaries of a cross build.

Usage: test-cross-artifacts.sh --target TARGET --builddir DIR
                               [--readelf PROG] [--objdump PROG]
                               [--dll-path DIR[:DIR...]]

Options:
  --target TARGET   linux-arm64, windows-amd64 or android-arm64
  --builddir DIR    the target's build directory (build/release-arm64, ...)
  --readelf PROG    readelf to use (ELF targets; default: readelf)
  --objdump PROG    objdump to use (windows-amd64; default: objdump)
  --dll-path DIRS   where libregnum.dll's imports may be found
                    (windows-amd64; colon-separated)
  -h, --help        show this help
  --license         show the license

Examples:
  make ARM64=1 test-cross-artifacts
  make test-cross-windows
  tests/test-cross-artifacts.sh --target linux-arm64 --builddir build/release-arm64
USAGE
}

target=''
builddir=''
readelf='readelf'
objdump='objdump'
dll_path=''
while [[ $# -gt 0 ]]
do
	case "${1}" in
		--target) target="${2:?}"; shift 2 ;;
		--builddir) builddir="${2:?}"; shift 2 ;;
		--readelf) readelf="${2:?}"; shift 2 ;;
		--objdump) objdump="${2:?}"; shift 2 ;;
		--dll-path) dll_path="${2:?}"; shift 2 ;;
		-h|--help) usage; exit 0 ;;
		--license) sed -n '2,16p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'; exit 0 ;;
		*) usage >&2; exit 2 ;;
	esac
done
[[ -n "${target}" && -n "${builddir}" ]] || { usage >&2; exit 2; }
[[ -d "${builddir}" ]] || { echo "test-cross-artifacts: no ${builddir} (build the target first)" >&2; exit 2; }

BS_NAME="test-cross-artifacts (${target})"
# shellcheck source=tests/build-system-lib.sh
source "$(dirname -- "${BASH_SOURCE[0]}")/build-system-lib.sh"

# The distinct "Machine:" values readelf reports for a file (an archive
# reports one per member), one per line
machines () {
	"${readelf}" -h "${1}" 2>/dev/null | sed -n 's/^ *Machine: *//p' | sort -u
}

# Every file given is AArch64 (and, for an archive, every member is)
check_aarch64 () {
	local file

	for file in "$@"
	do
		bs_eq "$(basename -- "${file}") is AArch64" 'AArch64' "$(machines "${file}")"
	done
}

# Every LOAD segment of an ELF file is aligned to at least 16 KB
check_16k () {
	local file="${1}"

	if "${readelf}" -lW "${file}" | gawk '$1 == "LOAD" { n++; if (strtonum ($NF) < 16384) bad = 1 } END { exit (bad || n == 0) }'
	then
		bs_pass "$(basename -- "${file}") LOAD segments 16 KB aligned"
	else
		bs_failed "$(basename -- "${file}") LOAD segments 16 KB aligned" "$("${readelf}" -lW "${file}" | grep LOAD)"
	fi
}

# A Windows file's object format, from objdump -f ("pei-x86-64" for PE32+)
pe_format () {
	"${objdump}" -f "${1}" 2>/dev/null | sed -n 's/.*file format *//p' | head -n 1
}

# The names in a DLL's export table
pe_exports () {
	"${objdump}" -p "${1}" | gawk '/^\[Ordinal\/Name Pointer\] Table/ { on = 1; next } on && NF == 0 { exit } on { print $NF }'
}

# A DLL name found (case-insensitively, as Windows looks) in any of the
# colon-separated directories
dll_found () {
	local name="${1}"
	local dir
	local dirs=()

	IFS=: read -r -a dirs <<< "${2}"
	for dir in "${dirs[@]}"
	do
		[[ -d "${dir}" ]] || continue
		if find "${dir}" -maxdepth 1 -iname "${name}" -print -quit | grep -q .
		then
			return 0
		fi
	done
	return 1
}

case "${target}" in
	linux-arm64)
		mapfile -t files < <(find "${builddir}/lib" "${builddir}/deps/graylib/lib" -maxdepth 1 -type f \
			\( -name '*.so*' -o -name '*.a' \) 2>/dev/null | sort)
		mapfile -t -O "${#files[@]}" files < <(find "${builddir}" -maxdepth 1 -type f -perm -u+x | sort)
		[[ ${#files[@]} -gt 0 ]] || bs_failed 'binaries present' "nothing under ${builddir}"
		check_aarch64 "${files[@]}"
		;;

	android-arm64)
		mapfile -t files < <(find "${builddir}/lib" "${builddir}/deps/graylib/lib" -maxdepth 1 -type f \
			\( -name '*.so*' -o -name '*.a' \) 2>/dev/null | sort)
		[[ ${#files[@]} -gt 0 ]] || bs_failed 'binaries present' "nothing under ${builddir}"
		check_aarch64 "${files[@]}"
		for file in "${files[@]}"
		do
			[[ "${file}" == *.so* ]] && check_16k "${file}"
		done
		;;

	windows-amd64)
		dll="${builddir}/lib/libregnum.dll"
		graylib="${builddir}/deps/graylib/lib/graylib.dll"
		for file in "${dll}" "${graylib}" "${builddir}"/*.exe
		do
			bs_eq "$(basename -- "${file}") is PE32+ x86-64" 'pei-x86-64' "$(pe_format "${file}")"
		done

		exports="$(pe_exports "${dll}")"
		bs_contains 'libregnum.dll exports lrg_engine_get_default' $'\nlrg_engine_get_default\n' $'\n'"${exports}"$'\n'
		exports="$(pe_exports "${graylib}")"
		bs_contains 'graylib.dll exports grl_window_new' $'\ngrl_window_new\n' $'\n'"${exports}"$'\n'
		bs_contains 'graylib.dll exports raylib (InitWindow)' $'\nInitWindow\n' $'\n'"${exports}"$'\n'

		# Every import resolves: the project's DLLs, the MinGW runtime,
		# or Windows itself (Wine's builtin DLLs stand in for it)
		search="${builddir}/lib:${builddir}/deps/graylib/lib:${dll_path}"
		wine_dlls="$(find /usr/lib64/wine-wow64/wine /usr/lib64/wine /usr/lib/wine -maxdepth 1 -type d -name 'x86_64-windows' 2>/dev/null | head -n 1)"
		missing=''
		while read -r name
		do
			[[ -z "${name}" ]] && continue
			# api-ms-win-* are API sets the loader maps, not files
			[[ "${name,,}" == api-ms-win-* ]] && continue
			dll_found "${name}" "${search}:${wine_dlls}" || missing+="${name} "
		done < <("${objdump}" -p "${dll}" | sed -n 's/^\s*DLL Name: *//p' | sort -u)
		bs_eq 'every DLL libregnum.dll imports is found' '' "${missing}"
		;;

	*)
		echo "test-cross-artifacts: unknown target '${target}'" >&2
		exit 2
		;;
esac

bs_finish
