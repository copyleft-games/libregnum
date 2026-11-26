#!/bin/bash
# run-emulator.sh - boot the Android emulator and run the device test on it
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

# Runs inside the test-emulator container (emulator-test.sh starts it with
# /dev/kvm).  Steps:
#
#   1. a fresh AVD in --work (a killed run leaves lock files that make the
#      next boot exit with "A snapshot operation ... is pending")
#   2. boots it headless: -gpu guest renders GLES in the guest with
#      SwiftShader (the host-side renderers need a GPU stack the container
#      does not have; emulator 37 segfaults with swiftshader_indirect
#      there), -feature -Vulkan, no audio, no snapshots, no metrics
#   3. waits for sys.boot_completed (--boot-timeout seconds)
#   4. tests/test-apk-device.sh on the APK, keeping its screenshot
#   5. kills the emulator; the exit status is the test's
#
# The emulator's own log is --work/emulator.log.

set -euo pipefail

usage () {
	cat <<'USAGE'
run-emulator.sh - boot an Android emulator and run tests/test-apk-device.sh.

Usage: run-emulator.sh --sdk DIR --work DIR --sysimg PATH --root DIR
                       --apk FILE --package ID [--shot FILE]
                       [--wait SECONDS] [--boot-timeout SECONDS]
                       [--data-size SIZE] [--device-script FILE --shot-dir DIR]

Options:
  --sdk DIR             SDK root holding emulator/, platform-tools/, system-images/
  --work DIR            scratch directory for the AVD, HOME and logs
  --sysimg PATH         system image, relative to the SDK root
                        (system-images/android-35/google_apis/x86_64)
  --root DIR            libregnum checkout (for tests/test-apk-device.sh)
  --apk FILE            the APK
  --package ID          its application id
  --shot FILE           keep the test's screenshot here
  --wait SECONDS        how long the game runs before the checks (default 15)
  --boot-timeout SECS   give up booting after this long (default 300)
  --data-size SIZE      the data partition (default 6G)
  --device-script FILE  run FILE instead of tests/test-apk-device.sh, with
                        --apk, --package, --wait and --shot-dir
  --shot-dir DIR        the device script's screenshot directory
  -h, --help            show this help
  --license             show the license

Example (emulator-test.sh runs it; by hand inside the container):
  run-emulator.sh --sdk build/android-emulator/sdk --work /tmp/emu \
      --sysimg system-images/android-35/google_apis/x86_64 --root . \
      --apk build/release-android-arm64/apk/game.apk --package org.libregnum.game
USAGE
}

sdk=''
work=''
sysimg=''
root=''
apk=''
package=''
shot=''
wait_seconds=15
boot_timeout=300
data_size='6G'
device_script=''
shot_dir=''
while [[ $# -gt 0 ]]
do
	case "${1}" in
		--sdk) sdk="${2:?}"; shift 2 ;;
		--work) work="${2:?}"; shift 2 ;;
		--sysimg) sysimg="${2:?}"; shift 2 ;;
		--root) root="${2:?}"; shift 2 ;;
		--apk) apk="${2:?}"; shift 2 ;;
		--package) package="${2:?}"; shift 2 ;;
		--shot) shot="${2:?}"; shift 2 ;;
		--wait) wait_seconds="${2:?}"; shift 2 ;;
		--boot-timeout) boot_timeout="${2:?}"; shift 2 ;;
		--data-size) data_size="${2:?}"; shift 2 ;;
		--device-script) device_script="${2:?}"; shift 2 ;;
		--shot-dir) shot_dir="${2:?}"; shift 2 ;;
		-h|--help) usage; exit 0 ;;
		--license) sed -n '2,16p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'; exit 0 ;;
		*) usage >&2; exit 2 ;;
	esac
done
[[ -n "${sdk}" && -n "${work}" && -n "${sysimg}" && -n "${root}" && -n "${apk}" && -n "${package}" ]] \
	|| { usage >&2; exit 2; }

emu_pid=''

# Stop the emulator however the script ends
# shellcheck disable=SC2329 # run by the EXIT trap
cleanup () {
	if [[ -n "${emu_pid}" ]] && kill -0 "${emu_pid}" 2>/dev/null
	then
		adb emu kill > /dev/null 2>&1 || kill "${emu_pid}" 2>/dev/null || true
		wait "${emu_pid}" 2>/dev/null || true
	fi
	adb kill-server > /dev/null 2>&1 || true
}
trap cleanup EXIT

# 1. a fresh AVD (and HOME: the emulator keeps state in ~/.android)
rm -rf -- "${work:?}/avd" "${work:?}/home"
mkdir -p -- "${work}/avd/test.avd" "${work}/home"
export ANDROID_SDK_ROOT="${sdk}" ANDROID_HOME="${sdk}" ANDROID_AVD_HOME="${work}/avd" HOME="${work}/home"
export PATH="${sdk}/platform-tools:${sdk}/emulator:${PATH}"

cat > "${work}/avd/test.ini" <<EOF
avd.ini.encoding=UTF-8
path=${work}/avd/test.avd
EOF
cat > "${work}/avd/test.avd/config.ini" <<EOF
image.sysdir.1=${sysimg}/
abi.type=$(basename -- "${sysimg}")
hw.cpu.arch=$(basename -- "${sysimg}" | sed 's/arm64-v8a/arm64/')
hw.ramSize=4096
hw.gpu.enabled=yes
hw.gpu.mode=guest
hw.lcd.width=1080
hw.lcd.height=1920
hw.lcd.density=420
hw.keyboard=yes
disk.dataPartition.size=${data_size}
EOF

# 2. boot
emulator -avd test -no-window -no-audio -no-boot-anim -no-snapshot -no-metrics \
	-gpu guest -feature -Vulkan -accel on > "${work}/emulator.log" 2>&1 &
emu_pid=$!

# 3. wait for the boot, failing early when the emulator dies
booted=0
for (( waited = 0; waited < boot_timeout; waited += 2 ))
do
	if ! kill -0 "${emu_pid}" 2>/dev/null
	then
		echo 'run-emulator: the emulator exited while booting; its log ends:' >&2
		tail -n 20 "${work}/emulator.log" >&2
		exit 1
	fi
	if [[ "$(adb shell getprop sys.boot_completed 2>/dev/null | tr -d '\r')" == '1' ]]
	then
		booted=1
		break
	fi
	sleep 2
done
if [[ "${booted}" -ne 1 ]]
then
	echo "run-emulator: not booted after ${boot_timeout}s; its log ends:" >&2
	tail -n 20 "${work}/emulator.log" >&2
	exit 1
fi
echo "run-emulator: booted in ${waited}s (Android $(adb shell getprop ro.build.version.release | tr -d '\r'), ABIs $(adb shell getprop ro.product.cpu.abilist | tr -d '\r'))"

# 4. the device test
rc=0
if [[ -n "${device_script}" ]]
then
	# the game's own checks: adb is on PATH and connected
	bash "${device_script}" --apk "${apk}" --package "${package}" \
		--wait "${wait_seconds}" --shot-dir "${shot_dir:?--device-script needs --shot-dir}" || rc=$?
else
	bash "${root}/tests/test-apk-device.sh" --apk "${apk}" --package "${package}" \
		--wait "${wait_seconds}" ${shot:+--keep-shot "${shot}"} || rc=$?
fi
exit "${rc}"
