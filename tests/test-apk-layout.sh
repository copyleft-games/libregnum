#!/bin/bash
# test-apk-layout.sh - check the structure of a libregnum game APK
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

# What a device needs from the APK, checked without a device:
#
#   signature       apksigner verify
#   manifest        package, minSdk, NativeActivity launcher, hasCode=false,
#                   extractNativeLibs=false, GLES 3.2
#   native library  lib/arm64-v8a/libmain.so stored (not deflated), on a
#                   16 KB boundary (zipalign -c -P 16), AArch64, every LOAD
#                   segment 16 KB aligned, ANativeActivity_onCreate exported,
#                   only system libraries NEEDED
#   assets          assets/lrg-assets.list well formed, every listed file
#                   present, and (with --assets) exactly the source trees,
#                   merged
#
# Runs in the android-arm64 build container (make ANDROID=1 test-apk).

set -euo pipefail

usage () {
	cat <<'USAGE'
test-apk-layout.sh - check an APK built by build-aux/android/mkapk.sh.

Usage: test-apk-layout.sh --apk FILE --package ID [--min-sdk N] [--assets DIR]...

Options:
  --apk FILE      the APK
  --package ID    the expected application id
  --min-sdk N     the expected minimum API (default 29)
  --assets DIR    a directory the assets came from (repeatable; the merged
                  trees are compared file by file)
  -h, --help      show this help
  --license       show the license

Examples:
  make ANDROID=1 test-apk APK_GAME=examples/game-chocolate-chip-clicker.c
USAGE
}

apk=''
package=''
min_sdk=29
assets=()
while [[ $# -gt 0 ]]
do
	case "${1}" in
		--apk) apk="${2:?}"; shift 2 ;;
		--package) package="${2:?}"; shift 2 ;;
		--min-sdk) min_sdk="${2:?}"; shift 2 ;;
		--assets) assets+=("${2:?}"); shift 2 ;;
		-h|--help) usage; exit 0 ;;
		--license) sed -n '2,16p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'; exit 0 ;;
		*) usage >&2; exit 2 ;;
	esac
done
[[ -n "${apk}" && -n "${package}" ]] || { usage >&2; exit 2; }

BS_NAME='test-apk-layout'
# shellcheck source=tests/build-system-lib.sh
source "$(dirname -- "${BASH_SOURCE[0]}")/build-system-lib.sh"

if [[ -f /opt/android/versions.env ]]
then
	# shellcheck source=/dev/null
	source /opt/android/versions.env
fi
tools="${ANDROID_BUILD_TOOLS:?ANDROID_BUILD_TOOLS is not set (run in the android-arm64 container)}"
readelf="${ANDROID_NDK:-/opt/android/ndk}/toolchains/llvm/prebuilt/linux-x86_64/bin/llvm-readelf"

work="$(mktemp -d "${TMPDIR:-/tmp}/lrg-apk-test.XXXXXX")"
trap 'rm -rf -- "${work}"' EXIT

echo '== signature'
if "${tools}/apksigner" verify "${apk}" > /dev/null 2>&1
then
	bs_pass 'apksigner verify'
else
	bs_failed 'apksigner verify'
fi

echo '== manifest'
badging="$("${tools}/aapt2" dump badging "${apk}" 2>/dev/null)"
xmltree="$("${tools}/aapt2" dump xmltree --file AndroidManifest.xml "${apk}" 2>/dev/null)"
bs_contains 'package' "package: name='${package}'" "${badging}"
bs_contains 'minimum API' "minSdkVersion:'${min_sdk}'" "${badging}"
bs_contains 'native code is arm64-v8a' "native-code: 'arm64-v8a'" "${badging}"
bs_contains 'launches NativeActivity' "launchable-activity: name='android.app.NativeActivity'" "${badging}"
bs_contains 'no Java code' 'hasCode(0x0101000c)=false' "${xmltree}"
bs_contains 'libraries mapped from the APK' 'extractNativeLibs(0x010104ea)=false' "${xmltree}"
bs_contains 'loads libmain.so' '"main"' "$(grep -A1 'android.app.lib_name' <<< "${xmltree}")"
bs_contains 'requires GLES 3.2' 'glEsVersion(0x01010281)=0x00030002' "${xmltree}"

echo '== native library'
listing="$(unzip -v "${apk}")"
bs_contains 'libmain.so present' 'lib/arm64-v8a/libmain.so' "${listing}"
bs_eq 'libmain.so stored, not deflated' 'Stored' \
	"$(awk '$NF == "lib/arm64-v8a/libmain.so" { print $2 }' <<< "${listing}")"
if "${tools}/zipalign" -c -P 16 4 "${apk}" > /dev/null 2>&1
then
	bs_pass 'zipalign -c -P 16'
else
	bs_failed 'zipalign -c -P 16'
fi
unzip -q -d "${work}/x" "${apk}" lib/arm64-v8a/libmain.so
so="${work}/x/lib/arm64-v8a/libmain.so"
bs_contains 'AArch64' 'AArch64' "$("${readelf}" -h "${so}")"
if "${readelf}" -lW "${so}" | gawk '$1 == "LOAD" { n++; if (strtonum($NF) < 16384) bad = 1 } END { exit (bad || n == 0) }'
then
	bs_pass 'every LOAD segment 16 KB aligned'
else
	bs_failed 'every LOAD segment 16 KB aligned'
fi
bs_contains 'ANativeActivity_onCreate exported' 'ANativeActivity_onCreate' \
	"$("${readelf}" -sW --dyn-syms "${so}" | awk '$7 != "UND"')"
needed="$("${readelf}" -dW "${so}" | sed -n 's/.*Shared library: \[\(.*\)\]/\1/p' | sort | tr '\n' ' ')"
unexpected="$(tr ' ' '\n' <<< "${needed}" | grep -vE '^(libc|libm|libdl|liblog|libandroid|libEGL|libGLESv[23]|libz|libOpenSLES|libaaudio)\.so$' | tr '\n' ' ' || true)"
bs_eq 'only system libraries NEEDED' '' "${unexpected// /}"

echo '== assets'
unzip -q -o -d "${work}/a" "${apk}" 'assets/*' 2>/dev/null || true
list="${work}/a/assets/lrg-assets.list"
if [[ -f "${list}" ]]
then
	bs_pass 'lrg-assets.list present'
	bs_contains 'manifest magic and stamp' '# lrg-assets ' "$(head -n 1 "${list}")"
	missing=''
	while IFS= read -r path
	do
		[[ -z "${path}" || "${path}" == '#'* ]] && continue
		[[ -f "${work}/a/assets/${path}" ]] || missing+="${path} "
	done < "${list}"
	bs_eq 'every listed asset is in the APK' '' "${missing}"
	if [[ ${#assets[@]} -gt 0 ]]
	then
		bs_eq 'assets match the source trees' \
			"$(for root in "${assets[@]}"; do (cd "${root}" && find . -type f -printf '%P\n'); done \
				| grep -vx 'lrg-assets.list' | LC_ALL=C sort)" \
			"$(tail -n +2 "${list}" | grep -v '^#' | grep . | LC_ALL=C sort || true)"
	fi
else
	bs_failed 'lrg-assets.list present'
fi

bs_finish
