#!/bin/bash
# emulator-test.sh - run a game APK on an Android emulator in a container
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

# `make ANDROID=1 test-emulator APK_GAME=...' runs this on the host.  It
# needs only podman (or CONTAINER_ENGINE), curl, sha1sum and a usable
# /dev/kvm; everything else runs in a container:
#
#   1. downloads the pinned emulator, platform-tools (adb) and system image
#      (build-aux/android/emulator/versions.sh) into --cache/downloads and
#      checks each SHA-1; an archive already there and correct is reused
#   2. builds the runtime image from build-aux/android/emulator/
#      Containerfile, tagged localhost/libregnum-android-emulator:<hash of
#      that directory and the base image>, unless it exists
#   3. unpacks the archives into --cache/sdk (once per pinned version)
#   4. runs build-aux/android/emulator/run-emulator.sh in the container
#      with /dev/kvm: boots the emulator, runs tests/test-apk-device.sh
#      on the APK, keeps the screenshot as --shot
#
# Exit status: the device test's (0 when it passed), 1 when the emulator
# fails to boot, 2 on bad usage or a missing prerequisite.

set -euo pipefail

usage () {
	cat <<'USAGE'
emulator-test.sh - run a game APK on an Android emulator in a container.

Usage: emulator-test.sh --apk FILE --package ID [--cache DIR] [--shot FILE]
                        [--wait SECONDS] [--boot-timeout SECONDS]

Options:
  --apk FILE            the APK (make ANDROID=1 apk APK_GAME=...)
  --package ID          its application id
  --cache DIR           downloads, unpacked SDK, AVD and logs
                        (default: build/android-emulator)
  --shot FILE           keep the screenshot here (default: next to the APK,
                        <name>-emulator.png)
  --wait SECONDS        how long the game runs before the checks (default 15)
  --boot-timeout SECS   give up booting after this long (default 300)
  -h, --help            show this help
  --license             show the license

Environment:
  CONTAINER_ENGINE      container CLI (default: podman)
  BUILD_CONTAINER_BASE  base image (default: registry.fedoraproject.org/fedora:45)

Examples:
  make ANDROID=1 test-emulator APK_GAME=examples/game-chocolate-chip-clicker.c
  build-aux/android/emulator-test.sh --apk build/release-android-arm64/apk/x.apk \
      --package org.libregnum.x
USAGE
}

die () {
	printf 'test-emulator: %s\n' "$*" >&2
	exit 2
}

script_dir="$(CDPATH='' cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
root="$(CDPATH='' cd -- "${script_dir}/../.." && pwd)"
emu_dir="${script_dir}/emulator"

apk=''
package=''
cache="${root}/build/android-emulator"
shot=''
wait_seconds=15
boot_timeout=300
while [[ $# -gt 0 ]]
do
	case "${1}" in
		--apk) apk="${2:?}"; shift 2 ;;
		--package) package="${2:?}"; shift 2 ;;
		--cache) cache="${2:?}"; shift 2 ;;
		--shot) shot="${2:?}"; shift 2 ;;
		--wait) wait_seconds="${2:?}"; shift 2 ;;
		--boot-timeout) boot_timeout="${2:?}"; shift 2 ;;
		-h|--help) usage; exit 0 ;;
		--license) sed -n '2,16p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'; exit 0 ;;
		*) usage >&2; exit 2 ;;
	esac
done
[[ -n "${apk}" && -n "${package}" ]] || { usage >&2; exit 2; }
[[ -f "${apk}" ]] || die "no ${apk} (make ANDROID=1 apk APK_GAME=... first)"

engine="${CONTAINER_ENGINE:-podman}"
base="${BUILD_CONTAINER_BASE:-registry.fedoraproject.org/fedora:45}"
command -v "${engine}" > /dev/null || die "${engine} not found"
command -v curl > /dev/null || die 'curl not found'
[[ -r /dev/kvm && -w /dev/kvm ]] || die '/dev/kvm is missing or not usable by you (the emulator needs KVM; check the kvm group)'

# shellcheck source=build-aux/android/emulator/versions.sh
source "${emu_dir}/versions.sh"

apk="$(realpath -- "${apk}")"
mkdir -p -- "${cache}/downloads"
cache="$(realpath -- "${cache}")"
[[ -n "${shot}" ]] || shot="${apk%.apk}-emulator.png"
mkdir -p -- "$(dirname -- "${shot}")"
shot="$(realpath -m -- "${shot}")"

# fetch <archive path in the repository> <sha1>
#
# Download one archive into the cache unless a copy with the right SHA-1
# is already there; a mismatch after downloading is fatal.
fetch () {
	if [[ $# -ne 2 ]]
	then
		die 'fetch() requires 2 positional arguments'
	fi

	local path="${1}"
	local sha1="${2}"
	local file

	file="${cache}/downloads/$(basename -- "${path}")"
	if [[ -f "${file}" ]] && echo "${sha1}  ${file}" | sha1sum -c --status
	then
		return 0
	fi
	echo "test-emulator: downloading ${path}"
	curl -fL --retry 3 -o "${file}.part" "${ANDROID_REPO_URL}/${path}"
	if ! echo "${sha1}  ${file}.part" | sha1sum -c --status
	then
		rm -f -- "${file}.part"
		die "${path}: SHA-1 mismatch (expected ${sha1})"
	fi
	mv -- "${file}.part" "${file}"
}

# 1. downloads
fetch "${EMULATOR_ZIP}" "${EMULATOR_SHA1}"
fetch "${PLATFORM_TOOLS_ZIP}" "${PLATFORM_TOOLS_SHA1}"
fetch "${EMULATOR_SYSIMG_ZIP}" "${EMULATOR_SYSIMG_SHA1}"

# 2. the runtime image, content-addressed like the build images
hash="$({ echo "${base}"; find "${emu_dir}" -type f | LC_ALL=C sort | xargs sha256sum; } | sha256sum | cut -c1-12)"
image="localhost/libregnum-android-emulator:${hash}"
if ! "${engine}" image exists "${image}"
then
	echo "test-emulator: building ${image}"
	"${engine}" build --build-arg "BASE_IMAGE=${base}" -t "${image}" -f "${emu_dir}/Containerfile" "${emu_dir}"
fi

run=("${engine}" run --rm --security-opt label=disable --device /dev/kvm
	-v "${root}:${root}" -v "${cache}:${cache}" -v "$(dirname -- "${apk}"):$(dirname -- "${apk}")"
	-v "$(dirname -- "${shot}"):$(dirname -- "${shot}")" -w "${root}" "${image}")

# 3. unpack once per pinned set (the stamp names every archive)
sysimg="system-images/android-${EMULATOR_SYSIMG_API}/${EMULATOR_SYSIMG_TAG}/${EMULATOR_SYSIMG_ABI}"
stamp="${EMULATOR_SHA1} ${PLATFORM_TOOLS_SHA1} ${EMULATOR_SYSIMG_SHA1}"
if [[ "$(cat -- "${cache}/sdk/.stamp" 2>/dev/null || true)" != "${stamp}" ]]
then
	echo 'test-emulator: unpacking the SDK'
	rm -rf -- "${cache}/sdk"
	mkdir -p -- "${cache}/sdk/$(dirname -- "${sysimg}")"
	# shellcheck disable=SC2016 # expanded by the container's shell
	"${run[@]}" bash -c 'set -e; unzip -qo "$1" -d "$4"; unzip -qo "$2" -d "$4"; unzip -qo "$3" -d "$5"' unpack \
		"${cache}/downloads/$(basename -- "${EMULATOR_ZIP}")" \
		"${cache}/downloads/$(basename -- "${PLATFORM_TOOLS_ZIP}")" \
		"${cache}/downloads/$(basename -- "${EMULATOR_SYSIMG_ZIP}")" \
		"${cache}/sdk" "${cache}/sdk/$(dirname -- "${sysimg}")"
	echo "${stamp}" > "${cache}/sdk/.stamp"
fi

# 4. boot and test
"${run[@]}" bash "${emu_dir}/run-emulator.sh" --sdk "${cache}/sdk" --work "${cache}/work" \
	--sysimg "${sysimg}" --root "${root}" --apk "${apk}" --package "${package}" \
	--shot "${shot}" --wait "${wait_seconds}" --boot-timeout "${boot_timeout}"
echo "test-emulator: screenshot ${shot}"
