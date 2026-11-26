#!/bin/bash
# test-emulator-args.sh - emulator-test.sh's options, without an emulator
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

# build-aux/android/emulator-test.sh --dry-run checks its arguments and
# prints the container command it would run, touching nothing: no
# download, no image, no /dev/kvm.  This checks that command for the
# default run and for a game's own device script (mounts, shot directory,
# data partition), and that bad arguments stop with status 2.
#
# Part of `make check-build-system' (docs/platforms/android.org, "A
# game's own checks on the emulator").

set -euo pipefail

BS_NAME='test-emulator-args'
# shellcheck source=tests/build-system-lib.sh
source "$(dirname -- "${BASH_SOURCE[0]}")/build-system-lib.sh"

script="$(CDPATH='' cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)/build-aux/android/emulator-test.sh"

work="$(mktemp -d)"
trap 'rm -rf -- "${work}"' EXIT
mkdir -p -- "${work}/apk" "${work}/data" "${work}/scripts"
: > "${work}/apk/game.apk"
printf '#!/bin/bash\nexit 0\n' > "${work}/scripts/device.sh"

# emu ARGS... -- the dry run's output, or "rc=<status>" when it failed
emu () {
	local out
	local rc=0

	out="$(CONTAINER_ENGINE=podman bash "${script}" --apk "${work}/apk/game.apk" \
		--package org.example.game --cache "${work}/cache" --dry-run "$@" 2>&1)" || rc=$?
	if [[ "${rc}" -ne 0 ]]
	then
		printf 'rc=%d %s' "${rc}" "${out}"
	else
		printf '%s' "${out}"
	fi
}

# 1. the default run: the built-in device test, the 6 GB partition, KVM
out="$(emu)"
bs_contains 'default: the emulator runs with /dev/kvm' '--device /dev/kvm' "${out}"
bs_contains 'default: run-emulator.sh' 'run-emulator.sh' "${out}"
bs_contains 'default: 6G data partition' '--data-size 6G' "${out}"
bs_not_contains 'default: no device script' '--device-script' "${out}"
bs_contains 'default: the APK directory is mounted' "-v ${work}/apk:${work}/apk" "${out}"

# 2. a game's own script, its data and screenshot directory
out="$(emu --device-script "${work}/scripts/device.sh" --shot-dir "${work}/shots" \
	--mount "${work}/data" --data-size 24G)"
bs_contains 'script: passed to run-emulator.sh' "--device-script ${work}/scripts/device.sh" "${out}"
bs_contains 'script: its directory is mounted read-only' "-v ${work}/scripts:${work}/scripts:ro" "${out}"
bs_contains 'script: the shot directory is passed' "--shot-dir ${work}/shots" "${out}"
bs_contains 'script: the shot directory is mounted read-write' "-v ${work}/shots:${work}/shots " "${out}"
bs_contains 'script: --mount is read-only at the same path' "-v ${work}/data:${work}/data:ro" "${out}"
bs_contains 'script: --data-size reaches the AVD' '--data-size 24G' "${out}"
if [[ -d "${work}/shots" ]]
then
	bs_pass 'script: the shot directory is created'
else
	bs_failed 'script: the shot directory is created'
fi

# 3. the default shot directory sits next to the APK
out="$(emu --device-script "${work}/scripts/device.sh")"
bs_contains 'script: default shot directory next to the APK' "--shot-dir ${work}/apk/game-emulator" "${out}"

# 4. bad arguments stop before anything runs
out="$(emu --data-size lots)"
bs_contains 'bad --data-size: status 2' 'rc=2' "${out}"
bs_contains 'bad --data-size: says why' 'give a size like 6G' "${out}"
out="$(emu --device-script "${work}/scripts/missing.sh")"
bs_contains 'missing --device-script: status 2' 'rc=2' "${out}"
out="$(emu --mount "${work}/nowhere")"
bs_contains 'missing --mount: status 2' 'rc=2' "${out}"

bs_finish
