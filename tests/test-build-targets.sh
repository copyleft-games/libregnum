#!/bin/bash
# test-build-targets.sh - the build target model and output directories
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

# Asserts how build-aux/mk/target.mk and config.mk resolve the target, the
# build directory and the profile defaults for every way of asking for a
# target.  Nothing is compiled and no container is started: every case
# reads variables through an audit makefile (tests/build-system-lib.sh).
# The host is pinned with LRG_HOST_TARGET, so the results are the same on
# an x86_64 and an aarch64 machine.

set -euo pipefail

usage () {
	cat <<'USAGE'
test-build-targets.sh - check target resolution and build directories.

Usage: test-build-targets.sh

Options:
  -h, --help  show this help
  --license   show the license

Examples:
  tests/test-build-targets.sh
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

BS_NAME='test-build-targets'
# shellcheck source=tests/build-system-lib.sh
source "$(dirname -- "${BASH_SOURCE[0]}")/build-system-lib.sh"

AMD=LRG_HOST_TARGET=linux-amd64
ARM=LRG_HOST_TARGET=linux-arm64
TARGET_VARS='LRG_TARGET LRG_IS_CROSS LRG_BUILD_SUFFIX BUILDDIR TARGET_PLATFORM'

# expect_target LABEL TARGET CROSS SUFFIX BUILDDIR PLATFORM -- MAKE_ARGS...
expect_target () {
	local label="${1}"
	local want
	local got
	want="$(printf 'LRG_TARGET=%s\nLRG_IS_CROSS=%s\nLRG_BUILD_SUFFIX=%s\nBUILDDIR=%s\nTARGET_PLATFORM=%s' \
		"${2}" "${3}" "${4}" "${5}" "${6}")"
	shift 7
	got="$(bs_vars "${TARGET_VARS}" -- "$@")"
	bs_eq "${label}" "${want}" "${got}"
}

echo '== native'
expect_target 'native release (amd64 host)' \
	linux-amd64 0 '' build/release linux -- "${AMD}"
expect_target 'native debug' \
	linux-amd64 0 '' build/debug linux -- "${AMD}" DEBUG=1
bs_eq 'SANITIZE=1 keeps build/sanitize' build/sanitize "$(bs_var BUILDDIR "${AMD}" SANITIZE=1)"
expect_target 'LRG_TARGET=<host> is native' \
	linux-amd64 0 '' build/release linux -- "${AMD}" LRG_TARGET=linux-amd64
bs_eq 'native GIR stays on' 1 "$(bs_var BUILD_GIR "${AMD}")"
bs_eq 'native has no profile' '' "$(bs_var LRG_TARGET_PROFILE "${AMD}")"

echo '== aliases'
for spelling in WINDOWS=1 LRG_TARGET=windows-amd64 CROSS=x86_64-w64-mingw32 'WINDOWS=1 CROSS=x86_64-w64-mingw32'
do
	# shellcheck disable=SC2086 # the spelling is one or two make arguments
	expect_target "${spelling} -> windows-amd64" \
		windows-amd64 1 -windows-amd64 build/release-windows-amd64 windows -- "${AMD}" ${spelling}
done
for spelling in ARM64=1 LRG_TARGET=linux-arm64
do
	expect_target "${spelling} -> linux-arm64" \
		linux-arm64 1 -arm64 build/release-arm64 linux -- "${AMD}" "${spelling}"
done
for spelling in ANDROID=1 LRG_TARGET=android-arm64
do
	expect_target "${spelling} -> android-arm64" \
		android-arm64 1 -android-arm64 build/release-android-arm64 android -- "${AMD}" "${spelling}"
done
bs_eq 'WINDOWS=0 is native' build/release "$(bs_var BUILDDIR "${AMD}" WINDOWS=0)"

echo '== release and debug directories of every cross target'
for pair in WINDOWS=1:windows-amd64 ARM64=1:arm64 ANDROID=1:android-arm64
do
	bs_eq "${pair%%:*} release" "build/release-${pair#*:}" "$(bs_var BUILDDIR "${AMD}" "${pair%%:*}")"
	bs_eq "${pair%%:*} debug" "build/debug-${pair#*:}" "$(bs_var BUILDDIR "${AMD}" "${pair%%:*}" DEBUG=1)"
	bs_eq "${pair%%:*} objects under its dir" "build/release-${pair#*:}/obj" "$(bs_var OBJDIR "${AMD}" "${pair%%:*}")"
	bs_eq "${pair%%:*} libraries under its dir" "build/release-${pair#*:}/lib" "$(bs_var LIBOUTDIR "${AMD}" "${pair%%:*}")"
done

echo '== host equal to target'
expect_target 'ARM64=1 on an aarch64 host is native' \
	linux-arm64 0 '' build/release linux -- "${ARM}" ARM64=1
expect_target 'aarch64 host, native default' \
	linux-arm64 0 '' build/debug linux -- "${ARM}" DEBUG=1
expect_target 'aarch64 host, WINDOWS=1 is still cross' \
	windows-amd64 1 -windows-amd64 build/release-windows-amd64 windows -- "${ARM}" WINDOWS=1

echo '== suffix handed in by the outer make'
bs_eq 'inside the container the given suffix is kept' build/release-arm64 \
	"$(LRG_IN_CONTAINER=1 LRG_BUILD_SUFFIX=-arm64 bs_var BUILDDIR "${ARM}" ARM64=1)"
bs_eq 'outside a container an environment suffix is ignored' build/release \
	"$(LRG_BUILD_SUFFIX=-bogus bs_var BUILDDIR "${AMD}")"

echo '== profile toolchain and defaults'
bs_eq 'windows CC' x86_64-w64-mingw32-gcc "$(bs_var CC "${AMD}" WINDOWS=1)"
bs_eq 'windows AR' x86_64-w64-mingw32-ar "$(bs_var AR "${AMD}" WINDOWS=1)"
bs_eq 'windows exe suffix' .exe "$(bs_var EXE_EXT "${AMD}" WINDOWS=1)"
bs_eq 'command-line CC beats the profile' my-gcc "$(bs_var CC "${AMD}" WINDOWS=1 CC=my-gcc)"
for alias in WINDOWS=1 ARM64=1 ANDROID=1
do
	bs_eq "${alias} turns GIR off" 0 "$(bs_var BUILD_GIR "${AMD}" "${alias}")"
	bs_contains "${alias} profile file" "build-aux/mk/targets/" "$(bs_var LRG_TARGET_PROFILE "${AMD}" "${alias}")"
done
bs_eq 'windows tests run under wine' wine "$(bs_var LRG_EXEC_WRAPPER "${AMD}" WINDOWS=1)"
bs_eq 'android container is amd64' linux/amd64 "$(bs_var LRG_CONTAINER_PLATFORM "${AMD}" ANDROID=1)"

echo '== subdirectory makefiles agree with the root'
for pair in '::build/release' 'WINDOWS=1::build/release-windows-amd64' 'ARM64=1:DEBUG=1:build/debug-arm64'
do
	IFS=: read -r a1 a2 want <<< "${pair}"
	for dir in tests examples bench
	do
		got="$(printf '%s' "${BS_AUDIT_MK}" | bs_make -s -C "${BS_ROOT}/${dir}" -f Makefile -f - \
			lrg-audit-vars LRG_AUDIT_VARS=BUILDDIR PKG_CONFIG=: "${AMD}" ${a1:+"${a1}"} ${a2:+"${a2}"} 2>/dev/null \
			| sed -n 's/^BUILDDIR=//p')"
		bs_eq "${dir}/ BUILDDIR with '${a1} ${a2}'" "${want}" "${got}"
	done
done

echo '== known targets and their directories'
bs_eq 'known targets' 'android-arm64 linux-arm64 windows-amd64' "$(bs_var LRG_KNOWN_TARGETS "${AMD}")"
bs_eq 'cross dirs (amd64 host)' \
	'build/release-android-arm64 build/debug-android-arm64 build/release-arm64 build/debug-arm64 build/release-windows-amd64 build/debug-windows-amd64' \
	"$(bs_var LRG_CROSS_BUILD_DIRS "${AMD}" | tr -s ' ')"
bs_not_contains 'cross dirs (aarch64 host) leave out its native arch' 'release-arm64' \
	"$(bs_var LRG_CROSS_BUILD_DIRS "${ARM}")"

echo '== rejected requests'
bs_fails 'two aliases' 'Conflicting targets' "${AMD}" ARM64=1 WINDOWS=1
bs_fails 'LRG_TARGET against an alias' 'Conflicting targets' "${AMD}" LRG_TARGET=linux-arm64 WINDOWS=1
bs_fails 'unknown target' "Unknown target 'plan9-mips'" "${AMD}" LRG_TARGET=plan9-mips
bs_fails 'unsupported CROSS triple' 'CROSS=arm-linux-gnueabi is not supported' "${AMD}" CROSS=arm-linux-gnueabi
bs_fails 'SANITIZE with a cross target' 'SANITIZE=1 is native-only' "${AMD}" SANITIZE=1 WINDOWS=1
bs_fails 'missing toolchain outside the container' 'toolchain is not installed' \
	"${AMD}" WINDOWS=1 PKG_CONFIG=/nonexistent/x86_64-w64-mingw32-pkg-config

echo '== goals that never compile work without the toolchain'
for alias in WINDOWS=1 ARM64=1 ANDROID=1
do
	out="$(bs_make -s -C "${BS_ROOT}" -f Makefile print-target "${AMD}" "${alias}" \
		PKG_CONFIG=/nonexistent/pkg-config 2>&1)" || true
	bs_contains "print-target for ${alias}" 'Build dir:' "${out}"
done
out="$(bs_make -s -C "${BS_ROOT}" -f Makefile targets "${AMD}" 2>&1)"
for t in linux-amd64 windows-amd64 linux-arm64 android-arm64
do
	bs_contains "targets lists ${t}" "${t}" "${out}"
done

bs_finish
