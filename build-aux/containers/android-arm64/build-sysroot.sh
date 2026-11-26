#!/bin/bash
# build-sysroot.sh - the static GLib stack for android arm64-v8a
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

# Called by setup.sh after the NDK is in /opt/android/ndk.  Cross-builds,
# as static libraries with .pc files, into PREFIX=/opt/android/arm64-v8a:
#
#   glib (+ libffi, pcre2, proxy-libintl through GLib's own meson wraps)
#   json-glib, libdex (optional: see below), fribidi (meson), libyaml and
#   sqlite (autotools), openssl (its own Configure)
#
# Everything is a static archive because the game ships as one shared
# object (libmain.so); pkg-config is then queried with --static so the
# private dependencies link too (build-aux/cross/lrg-sysroot-pkg-config).
# zlib, libm, liblog and libandroid come from the NDK.  Not built: libpq
# (the MMO server's PostgreSQL store) and libsoup (the analytics HTTP
# backend); libregnum builds those features off (LIBPQ=0, SOUP=0).
#
# libdex uses ucontext-based fibers, which bionic lacks; if it does not
# build, the stack goes on without it and libregnum's HAS_LIBDEX probe turns
# the async helpers off (it does the same on Windows).

set -euo pipefail

# shellcheck source=build-aux/containers/common/common.sh
source /usr/local/share/lrg-build/common/common.sh
# shellcheck source=build-aux/containers/android-arm64/versions.sh
source /usr/local/share/lrg-build/target/versions.sh

NDK=/opt/android/ndk
TOOLCHAIN="${NDK}/toolchains/llvm/prebuilt/linux-x86_64"
PREFIX=/opt/android/arm64-v8a
API="${ANDROID_MIN_API}"
WORK=/tmp/lrg-android-sysroot
CROSS_FILE="${WORK}/android-arm64.ini"
CC_BIN="${TOOLCHAIN}/bin/aarch64-linux-android${API}-clang"

mkdir -p -- "${WORK}" "${PREFIX}"

# fetch_source URL SHA256 -- download and unpack a source tarball, print
# its directory
fetch_source () {
	if [[ $# -ne 2 ]]
	then
		lrg_die 'fetch_source() requires URL SHA256'
	fi

	local url="${1}"
	local sha256="${2}"
	local file
	local dir

	file="${WORK}/$(basename -- "${url}")"
	curl -fsSL --retry 3 -o "${file}" "${url}" >&2 || lrg_die "download failed: ${url}"
	printf '%s  %s\n' "${sha256}" "${file}" | sha256sum -c - >&2 \
		|| lrg_die "checksum mismatch: ${url}"
	dir="$(tar -tf "${file}" | head -n 1 | cut -d/ -f1)"
	tar -xf "${file}" -C "${WORK}"
	printf '%s\n' "${WORK}/${dir}"
}

# The meson cross file: the NDK's clang for the arm64-v8a ABI at API $API,
# static libraries, this prefix
cat > "${CROSS_FILE}" <<EOF
[binaries]
c = '${CC_BIN}'
cpp = '${TOOLCHAIN}/bin/aarch64-linux-android${API}-clang++'
ar = '${TOOLCHAIN}/bin/llvm-ar'
strip = '${TOOLCHAIN}/bin/llvm-strip'
ranlib = '${TOOLCHAIN}/bin/llvm-ranlib'
pkg-config = 'pkg-config'

[properties]
pkg_config_libdir = ['${PREFIX}/lib/pkgconfig', '${PREFIX}/share/pkgconfig']
needs_exe_wrapper = true

[host_machine]
system = 'android'
cpu_family = 'aarch64'
cpu = 'aarch64'
endian = 'little'
EOF

# meson_build SRC ARGS... -- configure, build and install one meson project
meson_build () {
	if [[ $# -lt 1 ]]
	then
		lrg_die 'meson_build() requires SRC [ARGS...]'
	fi

	local src="${1}"
	shift

	lrg_log "meson: $(basename -- "${src}")"
	meson setup "${src}/_build" "${src}" \
		--cross-file="${CROSS_FILE}" \
		--prefix="${PREFIX}" --libdir=lib \
		--buildtype=release \
		--default-library=static \
		--wrap-mode=default \
		"$@" || return 1
	meson compile -C "${src}/_build" || return 1
	meson install -C "${src}/_build" --no-rebuild > /dev/null || return 1
}

# GLib: libffi, pcre2 and proxy-libintl from its wraps (bionic has iconv)
src="$(fetch_source "${GLIB_URL}" "${GLIB_SHA256}")"
meson_build "${src}" \
	--force-fallback-for=libffi,libpcre2-8,proxy-libintl \
	-Dintrospection=disabled -Dtests=false -Dnls=disabled \
	-Dselinux=disabled -Dlibmount=disabled -Dxattr=false \
	-Dman-pages=disabled -Ddocumentation=false -Dsysprof=disabled \
	|| lrg_die 'glib did not build for android'

src="$(fetch_source "${JSON_GLIB_URL}" "${JSON_GLIB_SHA256}")"
meson_build "${src}" \
	-Dintrospection=disabled -Dtests=false -Dgtk_doc=disabled -Dman=false -Dnls=disabled \
	|| lrg_die 'json-glib did not build for android'

src="$(fetch_source "${FRIBIDI_URL}" "${FRIBIDI_SHA256}")"
meson_build "${src}" -Ddocs=false -Dtests=false -Dbin=false \
	|| lrg_die 'fribidi did not build for android'

src="$(fetch_source "${LIBDEX_URL}" "${LIBDEX_SHA256}")"
if ! meson_build "${src}" -Dintrospection=disabled -Ddocs=false -Dexamples=false -Dtests=false -Dvapi=false
then
	lrg_log 'libdex does not build for android; libregnum builds without its async helpers'
	echo libdex >> /usr/local/share/lrg-build/missing-packages
fi

# autotools_build SRC ARGS... -- configure (for aarch64-linux-android with
# the NDK's clang), build and install one autotools project, static only
autotools_build () {
	if [[ $# -lt 1 ]]
	then
		lrg_die 'autotools_build() requires SRC [ARGS...]'
	fi

	local src="${1}"
	shift

	lrg_log "configure: $(basename -- "${src}")"
	(
		cd "${src}"
		CC="${CC_BIN}" AR="${TOOLCHAIN}/bin/llvm-ar" RANLIB="${TOOLCHAIN}/bin/llvm-ranlib" \
			./configure --host=aarch64-linux-android --prefix="${PREFIX}" --libdir="${PREFIX}/lib" \
			--disable-shared --enable-static "$@" > /dev/null
		make -j"$(nproc)" > /dev/null
		make install > /dev/null
	)
}

# libyaml: its release tarball's CMake files are incomplete
# (yamlConfig.cmake.in is missing); autotools also installs yaml-0.1.pc
src="$(fetch_source "${LIBYAML_URL}" "${LIBYAML_SHA256}")"
autotools_build "${src}" || lrg_die 'libyaml did not build for android'

# sqlite: the autoconf amalgamation
src="$(fetch_source "${SQLITE_URL}" "${SQLITE_SHA256}")"
autotools_build "${src}" --disable-readline || lrg_die 'sqlite did not build for android'

# OpenSSL: its own Configure knows the NDK (android-arm64), static only
src="$(fetch_source "${OPENSSL_URL}" "${OPENSSL_SHA256}")"
lrg_log 'configure: openssl'
(
	cd "${src}"
	export ANDROID_NDK_ROOT="${NDK}"
	export PATH="${TOOLCHAIN}/bin:${PATH}"
	./Configure android-arm64 -D__ANDROID_API__="${API}" \
		no-shared no-tests no-docs no-apps \
		--prefix="${PREFIX}" --libdir=lib > /dev/null
	make -j"$(nproc)" > /dev/null
	make install_sw > /dev/null
) || lrg_die 'openssl did not build for android'

# zlib is part of the NDK (libz) but has no .pc file there; libregnum asks
# pkg-config for it like on every other target
cat > "${PREFIX}/lib/pkgconfig/zlib.pc" <<EOF
prefix=${PREFIX}

Name: zlib
Description: zlib compression library (from the Android NDK)
Version: 1.3
Libs: -lz
Cflags:
EOF

rm -rf -- "${WORK}"
lrg_log "android sysroot: $(find "${PREFIX}/lib" -maxdepth 1 -name '*.a' | wc -l) static libraries in ${PREFIX}"
