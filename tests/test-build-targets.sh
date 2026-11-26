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
bs_eq 'MINIMAL_FEATURES=1 builds in build/minimal' build/minimal "$(bs_var BUILDDIR "${AMD}" MINIMAL_FEATURES=1)"
bs_eq 'MINIMAL_FEATURES=1 leaves libsoup out' 0 "$(bs_var SOUP "${AMD}" MINIMAL_FEATURES=1)"
bs_eq 'MINIMAL_FEATURES=1 leaves libpq out' 0 "$(bs_var LIBPQ "${AMD}" MINIMAL_FEATURES=1)"
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

echo '== bundled dependencies'
# Native: the submodules' own build directories, exactly as before cross
# targets existed (cmacs links against these paths)
R="${BS_ROOT}"
bs_eq 'native graylib archive' "${R}/deps/graylib/build/lib/libgraylib.a" "$(bs_var GRAYLIB_STATIC "${AMD}")"
bs_eq 'native raylib archive (in the submodule)' "${R}/deps/graylib/deps/raylib/src/libraylib.a" "$(bs_var RAYLIB_STATIC "${AMD}")"
bs_eq 'native yaml-glib archive' "${R}/deps/yaml-glib/build/release/libyaml-glib-1.0.a" "$(bs_var YAMLGLIB_STATIC "${AMD}")"
bs_eq 'native debug yaml-glib archive' "${R}/deps/yaml-glib/build/debug/libyaml-glib-1.0.a" "$(bs_var YAMLGLIB_STATIC "${AMD}" DEBUG=1)"
bs_eq 'native crispy archive' "${R}/deps/crispy/build/release/libcrispy.a" "$(bs_var CRISPY_STATIC "${AMD}")"
bs_eq 'native graylib flags are the shared ones' "$(bs_var DEP_BUILD_FLAGS "${AMD}")" "$(bs_var GRAYLIB_MAKE_FLAGS "${AMD}")"
bs_not_contains 'native graylib gets no BUILDDIR' 'BUILDDIR=' "$(bs_var GRAYLIB_MAKE_FLAGS "${AMD}")"
# Cross: everything under the target's own build directory
for pair in WINDOWS=1:release-windows-amd64 ARM64=1:release-arm64 'ANDROID=1 DEBUG=1':debug-android-arm64
do
	read -r -a args <<< "${pair%%:*}"
	d="${R}/build/${pair#*:}/deps"
	bs_eq "${pair%%:*} graylib archive" "${d}/graylib/lib/libgraylib.a" "$(bs_var GRAYLIB_STATIC "${AMD}" "${args[@]}")"
	bs_eq "${pair%%:*} raylib archive" "${d}/raylib/libraylib.a" "$(bs_var RAYLIB_STATIC "${AMD}" "${args[@]}")"
	bs_contains "${pair%%:*} yaml-glib archive" "${d}/yaml-glib/" "$(bs_var YAMLGLIB_STATIC "${AMD}" "${args[@]}")"
	bs_contains "${pair%%:*} crispy archive" "${d}/crispy/" "$(bs_var CRISPY_STATIC "${AMD}" "${args[@]}")"
	flags="$(bs_var GRAYLIB_MAKE_FLAGS "${AMD}" "${args[@]}")"
	bs_contains "${pair%%:*} graylib BUILDDIR" "BUILDDIR=${d}/graylib" "${flags}"
	bs_contains "${pair%%:*} graylib RAYLIB_BUILD_DIR" "RAYLIB_BUILD_DIR=${d}/raylib" "${flags}"
	bs_contains "${pair%%:*} yaml-glib BUILDDIR" "BUILDDIR=${d}/yaml-glib" "$(bs_var YAMLGLIB_MAKE_FLAGS "${AMD}" "${args[@]}")"
	bs_contains "${pair%%:*} crispy BUILDDIR" "BUILDDIR=${d}/crispy" "$(bs_var CRISPY_MAKE_FLAGS "${AMD}" "${args[@]}")"
done
bs_contains 'windows DEBUG reaches the deps' 'DEBUG=1' "$(bs_var GRAYLIB_MAKE_FLAGS "${AMD}" WINDOWS=1 DEBUG=1)"
bs_eq 'windows links graylib from its own dir' \
	"-L${R}/build/release-windows-amd64/deps/graylib/lib" \
	"$(bs_var ALL_LIBS "${AMD}" WINDOWS=1 | tr ' ' '\n' | grep -m1 -- '-L.*graylib')"

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
bs_eq 'windows tests run under wine, with a console' "${BS_ROOT}/build-aux/cross/lrg-wine-run" "$(bs_var LRG_EXEC_WRAPPER "${AMD}" WINDOWS=1)"
bs_eq 'android container is amd64' linux/amd64 "$(bs_var LRG_CONTAINER_PLATFORM "${AMD}" ANDROID=1)"

echo '== per-target defaults and runners'
for pair in BUILD_TOOLS:0 SOUP:0 LIBPQ:0 HAS_CRISPY:0 BUILD_EDITOR_UI:0 TARGET_PLATFORM:android
do
	bs_eq "android ${pair%%:*}" "${pair#*:}" "$(bs_var "${pair%%:*}" "${AMD}" ANDROID=1)"
done
bs_eq 'android dialect flag' '-Wno-typedef-redefinition' "$(bs_var LRG_DIALECT_WARN_CFLAGS "${AMD}" ANDROID=1)"
bs_contains 'android marks itself' '-DLRG_PLATFORM_ANDROID=1' "$(bs_var OPT_CFLAGS "${AMD}" ANDROID=1)"
bs_contains 'android marks libsoup off' '-DLRG_WITHOUT_SOUP=1' "$(bs_var OPT_CFLAGS "${AMD}" ANDROID=1)"
bs_contains 'android links GLES 3' '-lGLESv3' "$(bs_var PLATFORM_LIBS "${AMD}" ANDROID=1)"
bs_contains 'android 16 KB pages' 'max-page-size=16384' "$(bs_var LIB_LDFLAGS "${AMD}" ANDROID=1)"
bs_contains 'android graylib gets raylib platform' 'RAYLIB_PLATFORM=PLATFORM_ANDROID' "$(bs_var GRAYLIB_MAKE_FLAGS "${AMD}" ANDROID=1)"
for pair in BUILD_TOOLS:1 SOUP:1 LIBPQ:1
do
	bs_eq "native ${pair%%:*}" "${pair#*:}" "$(bs_var "${pair%%:*}" "${AMD}")"
done
bs_not_contains 'native marks nothing off' 'LRG_WITHOUT' "$(bs_var OPT_CFLAGS "${AMD}")"
bs_eq 'native runs tests directly' '' "$(bs_var LRG_EXEC_WRAPPER "${AMD}")"
# LRG_TEST_ARGS lives in tests/Makefile
tests_var () {
	printf '%s' "${BS_AUDIT_MK}" | bs_make -s -C "${BS_ROOT}/tests" -f Makefile -f - \
		lrg-audit-vars LRG_AUDIT_VARS="${1}" PKG_CONFIG=: NO_BUILD_CONTAINER=1 LRG_TOOLCHAIN_CHECK=true \
		"${@:2}" 2>/dev/null | sed -n "s/^${1}=//p"
}
bs_eq 'native skips no test paths' '' "$(tests_var LRG_TEST_ARGS "${AMD}")"
bs_contains 'arm64 tests run under qemu' 'qemu-aarch64-static -L' "$(bs_var LRG_EXEC_WRAPPER "${AMD}" ARM64=1)"
bs_contains 'arm64 CC is the sysroot wrapper' 'build-aux/cross/lrg-aarch64-linux-gnu-gcc' "$(bs_var CC "${AMD}" ARM64=1)"
bs_contains 'arm64 skips an emulation limit' '-s /broadphase/perf/10k' "$(tests_var LRG_TEST_ARGS "${AMD}" ARM64=1)"
bs_contains 'arm64 skips a known bug' '-s /replay/golden/physics-timers-rng' "$(tests_var LRG_TEST_ARGS "${AMD}" ARM64=1)"
bs_contains 'windows Wine finds the DLLs' 'build/release-windows-amd64/lib;' "$(bs_var WINEPATH "${AMD}" WINDOWS=1)"
bs_contains 'windows links Winsock' '-lws2_32' "$(bs_var PLATFORM_LIBS "${AMD}" WINDOWS=1)"
bs_eq 'windows asks for no gio-unix' '' "$(bs_var LRG_GIO_UNIX_PKG "${AMD}" WINDOWS=1)"
bs_eq 'native asks for gio-unix' 'gio-unix-2.0' "$(bs_var LRG_GIO_UNIX_PKG "${AMD}")"
bs_contains 'cross mcp-glib under deps' 'build/release-arm64/deps/mcp-glib' "$(bs_var MCPGLIB_BUILDDIR "${AMD}" ARM64=1)"
bs_eq 'native mcp-glib in its submodule' "${BS_ROOT}/deps/mcp-glib/build" "$(bs_var MCPGLIB_BUILDDIR "${AMD}")"
windows_tests="$(printf '%s' "${BS_AUDIT_MK}" | bs_make -s -C "${BS_ROOT}/tests" -f Makefile -f - \
	lrg-audit-vars LRG_AUDIT_VARS=TEST_SOURCES PKG_CONFIG=: NO_BUILD_CONTAINER=1 LRG_TOOLCHAIN_CHECK=true \
	"${AMD}" WINDOWS=1 2>/dev/null)"
bs_not_contains 'windows leaves out a POSIX-only suite' 'test-vfs.c' "${windows_tests}"
bs_contains 'windows keeps a portable suite' 'test-platform.c' "${windows_tests}"

echo '== subdirectory makefiles agree with the root'
for pair in '::build/release' 'WINDOWS=1::build/release-windows-amd64' 'ARM64=1:DEBUG=1:build/debug-arm64'
do
	IFS=: read -r a1 a2 want <<< "${pair}"
	for dir in tests examples bench
	do
		got="$(printf '%s' "${BS_AUDIT_MK}" | bs_make -s -C "${BS_ROOT}/${dir}" -f Makefile -f - \
			lrg-audit-vars LRG_AUDIT_VARS=BUILDDIR PKG_CONFIG=: NO_BUILD_CONTAINER=1 LRG_TOOLCHAIN_CHECK=true "${AMD}" ${a1:+"${a1}"} ${a2:+"${a2}"} 2>/dev/null \
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
bs_fails 'MINIMAL_FEATURES with a cross target' 'MINIMAL_FEATURES=1 is native-only' "${AMD}" MINIMAL_FEATURES=1 ARM64=1
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
