#!/bin/bash
# test-apk-device.sh - run a libregnum game APK on a device and check it
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

# `make ANDROID=1 test-device' (on the host, where adb sees the device):
#
#   1. installs the APK and starts its NativeActivity
#   2. after --wait seconds the process is still alive and its activity
#      has the window focus (a finished activity leaves a cached process
#      behind and the launcher on screen, which steps 2-4 alone would pass)
#   3. logcat holds no warning, error or fatal line from the app's GLib log
#      domains (Libregnum-*, GLib*, Graylib*, yaml-glib: the platform layer
#      sends GLib's log to logcat under the domain as tag)
#   4. a screenshot is not one flat colour (something was drawn)
#   5. stops the app
#
# With no adb, or no device attached, it prints why and exits 0 (skipped):
# the APK's structure is still checked without one (make test-apk).
#
# Exit status: 0 when every check passed (or skipped), 1 otherwise.

set -euo pipefail

usage () {
	cat <<'USAGE'
test-apk-device.sh - run a game APK on an attached device and check it.

Usage: test-apk-device.sh --apk FILE --package ID [--wait SECONDS] [--keep-shot FILE]

Options:
  --apk FILE        the APK (make ANDROID=1 apk APK_GAME=...)
  --package ID      its application id
  --wait SECONDS    how long the game runs before the checks (default 8)
  --keep-shot FILE  keep the screenshot there (default: discarded)
  -h, --help        show this help
  --license         show the license

Examples:
  make ANDROID=1 apk APK_GAME=examples/game-chocolate-chip-clicker.c
  make ANDROID=1 test-device APK_GAME=examples/game-chocolate-chip-clicker.c
  ANDROID_SERIAL=emulator-5554 tests/test-apk-device.sh --apk build/release-android-arm64/apk/x.apk --package org.libregnum.x
USAGE
}

apk=''
package=''
wait_seconds=8
keep_shot=''
while [[ $# -gt 0 ]]
do
	case "${1}" in
		--apk) apk="${2:?}"; shift 2 ;;
		--package) package="${2:?}"; shift 2 ;;
		--wait) wait_seconds="${2:?}"; shift 2 ;;
		--keep-shot) keep_shot="${2:?}"; shift 2 ;;
		-h|--help) usage; exit 0 ;;
		--license) sed -n '2,16p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'; exit 0 ;;
		*) usage >&2; exit 2 ;;
	esac
done
[[ -n "${apk}" && -n "${package}" ]] || { usage >&2; exit 2; }
[[ -f "${apk}" ]] || { echo "test-apk-device: no ${apk} (make ANDROID=1 apk first)" >&2; exit 2; }

BS_NAME='test-apk-device'
# shellcheck source=tests/build-system-lib.sh
source "$(dirname -- "${BASH_SOURCE[0]}")/build-system-lib.sh"

# Skips, with the reason, when there is nothing to run on
if ! command -v adb > /dev/null
then
	echo 'test-apk-device: SKIP: adb not found (Fedora: android-tools)'
	exit 0
fi
if [[ "$(adb get-state 2> /dev/null || true)" != 'device' ]]
then
	echo 'test-apk-device: SKIP: no device or emulator attached (adb devices)'
	exit 0
fi

work="$(mktemp -d)"
trap 'rm -rf -- "${work}"' EXIT

# 1. install and start
if adb install -r "${apk}" > "${work}/install.log" 2>&1
then
	bs_pass 'adb install'
else
	bs_failed 'adb install' "$(cat "${work}/install.log")"
	bs_finish
fi
adb logcat -c
adb shell am force-stop "${package}"
adb shell am start -W -n "${package}/android.app.NativeActivity" > /dev/null
sleep "${wait_seconds}"

# 2. still running
pid="$(adb shell pidof "${package}" | tr -d '\r' || true)"
if [[ -n "${pid}" ]]
then
	bs_pass "running after ${wait_seconds}s (pid ${pid})"
else
	bs_failed "running after ${wait_seconds}s" "$(adb logcat -d -b crash | tail -n 40)"
fi
focus="$(adb shell dumpsys window 2> /dev/null | tr -d '\r' | grep -m1 -E 'mCurrentFocus=' || true)"
if [[ "${focus}" == *"${package}/"* ]]
then
	bs_pass 'the activity has the window focus'
else
	bs_failed 'the activity has the window focus' \
		"focus: ${focus:-none}$(printf '\n')$(adb logcat -d -v brief | tail -n 40)"
fi

# 3. no GLib warning or worse from the app ("brief" format: W/Tag( pid): ...)
adb logcat -d -v brief > "${work}/logcat.txt"
bad="$(grep -E '^[WEF]/(Libregnum|libregnum|GLib|Graylib|graylib|yaml-glib|Yaml)[^(]*\(' "${work}/logcat.txt" || true)"
bs_eq 'no GLib warnings, errors or criticals in logcat' '' "${bad}"

# 4. something was drawn: a flat-colour PNG decompresses to a handful of
# distinct byte values (its rows repeat), a rendered frame to far more
adb exec-out screencap -p > "${work}/shot.png"
distinct="$(python3 -I - "${work}/shot.png" <<'PY'
import struct, sys, zlib
data = open(sys.argv[1], 'rb').read()
pos, idat = 8, b''
while pos < len(data):
    length, kind = struct.unpack('>I4s', data[pos:pos + 8])
    if kind == b'IDAT':
        idat += data[pos + 8:pos + 8 + length]
    pos += 12 + length
print(len(set(zlib.decompress(idat))) if idat else 0)
PY
)"
if [[ "${distinct}" -gt 16 ]]
then
	bs_pass "screenshot is not blank (${distinct} distinct byte values)"
else
	bs_failed 'screenshot is not blank' "only ${distinct} distinct byte values"
fi
[[ -n "${keep_shot}" ]] && cp -- "${work}/shot.png" "${keep_shot}"

# 5. stop
adb shell am force-stop "${package}"

bs_finish
