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
# A game with more to check than "it starts and draws" (data to copy onto
# the device first, input to send, several screenshots) passes its own
# --device-script: it runs in place of tests/test-apk-device.sh, with adb
# connected to the booted emulator, and gets --apk, --package, --wait and
# --shot-dir.  --mount makes host directories (the game's own data, never
# part of the APK) readable in the container at the same path, and
# --data-size grows the emulator's data partition to hold them.
#
# Exit status: the device test's (0 when it passed), 1 when the emulator
# fails to boot, 2 on bad usage or a missing prerequisite.

set -euo pipefail

usage () {
	cat <<'USAGE'
emulator-test.sh - run a game APK on an Android emulator in a container.

Usage: emulator-test.sh --apk FILE --package ID [--cache DIR] [--shot FILE]
                        [--wait SECONDS] [--boot-timeout SECONDS]
                        [--device-script FILE] [--shot-dir DIR]
                        [--mount DIR]... [--data-size SIZE]

Options:
  --apk FILE            the APK (make ANDROID=1 apk APK_GAME=...)
  --package ID          its application id
  --cache DIR           downloads, unpacked SDK, AVD and logs
                        (default: build/android-emulator)
  --shot FILE           keep the screenshot here (default: next to the APK,
                        <name>-emulator.png)
  --wait SECONDS        how long the game runs before the checks (default 15)
  --boot-timeout SECS   give up booting after this long (default 300)
  --device-script FILE  run FILE on the booted emulator instead of
                        tests/test-apk-device.sh; it gets --apk, --package,
                        --wait and --shot-dir, adb is on PATH and connected
  --shot-dir DIR        where a device script keeps its screenshots
                        (default: <apk dir>/<name>-emulator/)
  --mount DIR           a host directory the device script reads (e.g. game
                        data to adb push), mounted read-only at the same
                        path (repeatable)
  --data-size SIZE      the emulator's data partition (default 6G)
  --dry-run             check the arguments and print the container command
                        that would run the emulator; download, build and
                        boot nothing (no /dev/kvm needed)
  -h, --help            show this help
  --license             show the license

Environment:
  CONTAINER_ENGINE      container CLI (default: podman)
  BUILD_CONTAINER_BASE  base image (default: registry.fedoraproject.org/fedora:45)

Examples:
  make ANDROID=1 test-emulator APK_GAME=examples/game-chocolate-chip-clicker.c
  build-aux/android/emulator-test.sh --apk build/release-android-arm64/apk/x.apk \
      --package org.libregnum.x
  build-aux/android/emulator-test.sh --apk game.apk --package org.example.game \
      --device-script tests/android/emulator-run.sh --mount ~/game-data --data-size 24G
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
device_script=''
shot_dir=''
data_size='6G'
mounts=()
dry_run=0
while [[ $# -gt 0 ]]
do
	case "${1}" in
		--apk) apk="${2:?}"; shift 2 ;;
		--package) package="${2:?}"; shift 2 ;;
		--cache) cache="${2:?}"; shift 2 ;;
		--shot) shot="${2:?}"; shift 2 ;;
		--wait) wait_seconds="${2:?}"; shift 2 ;;
		--boot-timeout) boot_timeout="${2:?}"; shift 2 ;;
		--device-script) device_script="${2:?}"; shift 2 ;;
		--shot-dir) shot_dir="${2:?}"; shift 2 ;;
		--mount) mounts+=("${2:?}"); shift 2 ;;
		--data-size) data_size="${2:?}"; shift 2 ;;
		--dry-run) dry_run=1; shift ;;
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
[[ "${dry_run}" -eq 1 ]] || command -v curl > /dev/null || die 'curl not found'
[[ "${dry_run}" -eq 1 ]] || [[ -r /dev/kvm && -w /dev/kvm ]] || die '/dev/kvm is missing or not usable by you (the emulator needs KVM; check the kvm group)'

# shellcheck source=build-aux/android/emulator/versions.sh
source "${emu_dir}/versions.sh"

apk="$(realpath -- "${apk}")"
mkdir -p -- "${cache}/downloads"
cache="$(realpath -- "${cache}")"
[[ -n "${shot}" ]] || shot="${apk%.apk}-emulator.png"
mkdir -p -- "$(dirname -- "${shot}")"
shot="$(realpath -m -- "${shot}")"
[[ "${data_size}" =~ ^[0-9]+[MG]$ ]] || die "--data-size ${data_size}: give a size like 6G or 512M"

# A device script, the directory it may write and the directories it reads
extra_mounts=()
if [[ -n "${device_script}" ]]
then
	[[ -f "${device_script}" ]] || die "--device-script ${device_script}: no such file"
	device_script="$(realpath -- "${device_script}")"
	[[ -n "${shot_dir}" ]] || shot_dir="${apk%.apk}-emulator"
	mkdir -p -- "${shot_dir}"
	shot_dir="$(realpath -- "${shot_dir}")"
	extra_mounts+=(-v "$(dirname -- "${device_script}"):$(dirname -- "${device_script}"):ro"
		-v "${shot_dir}:${shot_dir}")
fi
for dir in "${mounts[@]}"
do
	[[ -d "${dir}" ]] || die "--mount ${dir}: no such directory"
	dir="$(realpath -- "${dir}")"
	extra_mounts+=(-v "${dir}:${dir}:ro")
done

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
if [[ "${dry_run}" -eq 0 ]]
then
	fetch "${EMULATOR_ZIP}" "${EMULATOR_SHA1}"
	fetch "${PLATFORM_TOOLS_ZIP}" "${PLATFORM_TOOLS_SHA1}"
	fetch "${EMULATOR_SYSIMG_ZIP}" "${EMULATOR_SYSIMG_SHA1}"
fi

# 2. the runtime image, content-addressed like the build images
hash="$({ echo "${base}"; find "${emu_dir}" -type f | LC_ALL=C sort | xargs sha256sum; } | sha256sum | cut -c1-12)"
image="localhost/libregnum-android-emulator:${hash}"
if [[ "${dry_run}" -eq 0 ]] && ! "${engine}" image exists "${image}"
then
	echo "test-emulator: building ${image}"
	"${engine}" build --build-arg "BASE_IMAGE=${base}" -t "${image}" -f "${emu_dir}/Containerfile" "${emu_dir}"
fi

run=("${engine}" run --rm --security-opt label=disable --device /dev/kvm
	-v "${root}:${root}" -v "${cache}:${cache}" -v "$(dirname -- "${apk}"):$(dirname -- "${apk}")"
	-v "$(dirname -- "${shot}"):$(dirname -- "${shot}")" "${extra_mounts[@]}" -w "${root}" "${image}")

# 3. unpack once per pinned set (the stamp names every archive)
sysimg="system-images/android-${EMULATOR_SYSIMG_API}/${EMULATOR_SYSIMG_TAG}/${EMULATOR_SYSIMG_ABI}"
stamp="${EMULATOR_SHA1} ${PLATFORM_TOOLS_SHA1} ${EMULATOR_SYSIMG_SHA1}"
if [[ "${dry_run}" -eq 0 && "$(cat -- "${cache}/sdk/.stamp" 2>/dev/null || true)" != "${stamp}" ]]
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
emu_cmd=("${run[@]}" bash "${emu_dir}/run-emulator.sh" --sdk "${cache}/sdk" --work "${cache}/work" \
	--sysimg "${sysimg}" --root "${root}" --apk "${apk}" --package "${package}" \
	--shot "${shot}" --wait "${wait_seconds}" --boot-timeout "${boot_timeout}" \
	--data-size "${data_size}" \
	${device_script:+--device-script "${device_script}" --shot-dir "${shot_dir}"})
if [[ "${dry_run}" -eq 1 ]]
then
	printf '%q ' "${emu_cmd[@]}"
	printf '\n'
	exit 0
fi
"${emu_cmd[@]}"
if [[ -n "${device_script}" ]]
then
	echo "test-emulator: screenshots in ${shot_dir}"
else
	echo "test-emulator: screenshot ${shot}"
fi
