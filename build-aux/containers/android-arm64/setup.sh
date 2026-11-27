#!/bin/bash
# setup.sh - provision the android-arm64 build container
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

# Runs once, inside `podman build' (always linux/amd64: the NDK ships x86_64
# host tools only).  Steps:
#
#   1. build-side packages: meson/ninja/cmake for the GLib stack, a JDK for
#      apksigner and keytool, zip/unzip for APKs
#   2. the Android SDK pieces pinned in versions.sh, each checked against
#      the SHA-1 in Google's repository manifest:
#        /opt/android/ndk                         the NDK
#        /opt/android/sdk/build-tools/<version>   aapt2, zipalign, apksigner
#        /opt/android/sdk/platforms/android-<api> android.jar
#   3. build-sysroot.sh: the static GLib stack in /opt/android/arm64-v8a
#   4. smoke tests: a GLib + json-glib program, and LuaJIT + bzip2 +
#      libjpeg-turbo (smoke-extras.c) as a program and a shared object,
#      linked for arm64-v8a
#      (an Android binary cannot run here; its ELF headers are checked)

set -euo pipefail

# shellcheck source=build-aux/containers/common/common.sh
source /usr/local/share/lrg-build/common/common.sh
# shellcheck source=build-aux/containers/android-arm64/versions.sh
source /usr/local/share/lrg-build/target/versions.sh

ANDROID_ROOT=/opt/android
SDK="${ANDROID_ROOT}/sdk"

lrg_install_base
lrg_dnf_install \
	meson ninja-build cmake perl glslang \
	java-25-openjdk-headless \
	unzip zip curl ca-certificates

# fetch_verified FILE SHA1 DEST -- download an SDK archive and unpack it
# into DEST (its single top-level directory becomes DEST)
fetch_verified () {
	if [[ $# -ne 3 ]]
	then
		lrg_die 'fetch_verified() requires FILE SHA1 DEST'
	fi

	local file="${1}"
	local sha1="${2}"
	local dest="${3}"
	local tmp
	local top

	tmp="$(mktemp -d)"
	lrg_log "download ${ANDROID_REPO_URL}/${file}"
	curl -fsSL --retry 3 -o "${tmp}/${file}" "${ANDROID_REPO_URL}/${file}" \
		|| lrg_die "download failed: ${file}"
	printf '%s  %s\n' "${sha1}" "${tmp}/${file}" | sha1sum -c - \
		|| lrg_die "checksum mismatch: ${file}"
	unzip -q "${tmp}/${file}" -d "${tmp}/x"
	top="$(find "${tmp}/x" -mindepth 1 -maxdepth 1)"
	if [[ "$(printf '%s\n' "${top}" | wc -l)" -ne 1 || ! -d "${top}" ]]
	then
		lrg_die "${file}: expected one top-level directory"
	fi
	mkdir -p -- "$(dirname -- "${dest}")"
	mv -- "${top}" "${dest}"
	rm -rf -- "${tmp}"
}

fetch_verified "${ANDROID_NDK_ZIP}" "${ANDROID_NDK_SHA1}" "${ANDROID_ROOT}/ndk"
fetch_verified "${ANDROID_BUILD_TOOLS_ZIP}" "${ANDROID_BUILD_TOOLS_SHA1}" \
	"${SDK}/build-tools/${ANDROID_BUILD_TOOLS_VERSION}"
fetch_verified "${ANDROID_PLATFORM_ZIP}" "${ANDROID_PLATFORM_SHA1}" \
	"${SDK}/platforms/android-${ANDROID_PLATFORM_API}"

# What the profile and the APK packager read (build-aux/mk/targets/
# android-arm64.mk, build-aux/android/mkapk.sh)
cat > "${ANDROID_ROOT}/versions.env" <<EOF
ANDROID_NDK=${ANDROID_ROOT}/ndk
ANDROID_NDK_VERSION=${ANDROID_NDK_VERSION}
ANDROID_SDK=${SDK}
ANDROID_BUILD_TOOLS=${SDK}/build-tools/${ANDROID_BUILD_TOOLS_VERSION}
ANDROID_PLATFORM_JAR=${SDK}/platforms/android-${ANDROID_PLATFORM_API}/android.jar
ANDROID_MIN_API=${ANDROID_MIN_API}
EOF

bash /usr/local/share/lrg-build/target/build-sysroot.sh

# Smoke test: link a GLib program against the static stack
install -m 755 /usr/local/share/lrg-build/cross/lrg-sysroot-pkg-config /usr/local/bin/
export LRG_SYSROOT="${ANDROID_ROOT}/arm64-v8a"
export LRG_SYSROOT_LIBDIRS=lib/pkgconfig:share/pkgconfig
export LRG_SYSROOT_PREFIXED=0
export LRG_PKG_CONFIG_STATIC=1
clang="${ANDROID_ROOT}/ndk/toolchains/llvm/prebuilt/linux-x86_64/bin/aarch64-linux-android${ANDROID_MIN_API}-clang"
readelf="${ANDROID_ROOT}/ndk/toolchains/llvm/prebuilt/linux-x86_64/bin/llvm-readelf"
lrg_smoke "${clang}" lrg-sysroot-pkg-config /tmp/smoke-android
# The header is read whole into a string, never piped into grep -q: grep -q
# exits on the first match, llvm-readelf then dies of SIGPIPE writing the
# rest and pipefail fails the check (about one run in 30)
[[ "$("${readelf}" -h /tmp/smoke-android)" == *AArch64* ]] || lrg_die 'smoke binary is not AArch64'
# Android 15+ devices may use 16 KB pages: every LOAD segment must allow it
if ! "${readelf}" -lW /tmp/smoke-android \
	| gawk '$1 == "LOAD" { n++; if (strtonum($NF) < 16384) bad = 1 } END { exit (bad || n == 0) }'
then
	lrg_die 'smoke binary has a LOAD segment not aligned to 16 KB'
fi
rm -f -- /tmp/smoke-android

# Smoke test: LuaJIT, bzip2 and libjpeg-turbo through their .pc files, as
# an executable and as a shared object like libmain.so (a non-PIC archive
# fails the second link)
extras="/usr/local/share/lrg-build/target/smoke-extras.c"
flags="$(lrg-sysroot-pkg-config --cflags --libs luajit bzip2 libjpeg)" \
	|| lrg_die 'lrg-sysroot-pkg-config does not find luajit, bzip2 and libjpeg'
lrg_log "smoke test: ${extras}"
# shellcheck disable=SC2086 # pkg-config output is a word list
"${clang}" -std=gnu89 -Wall -Wextra -Werror -o /tmp/smoke-extras "${extras}" ${flags} \
	|| lrg_die 'cannot link a program against luajit, bzip2 and libjpeg'
# shellcheck disable=SC2086 # pkg-config output is a word list
"${clang}" -std=gnu89 -Wall -Wextra -Werror -DSMOKE_SHARED -shared -fPIC \
	-Wl,--no-undefined -o /tmp/smoke-extras.so "${extras}" ${flags} \
	|| lrg_die 'cannot link a shared object against luajit, bzip2 and libjpeg'
[[ "$("${readelf}" -h /tmp/smoke-extras)" == *AArch64* ]] || lrg_die 'smoke-extras is not AArch64'
rm -f -- /tmp/smoke-extras /tmp/smoke-extras.so
lrg_log 'android-arm64 toolchain ready'
