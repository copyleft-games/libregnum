#!/bin/bash
# mkapk.sh - package a libregnum game's libmain.so and assets into an APK
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

# No Gradle, no Java code: a NativeActivity APK is a manifest, a native
# library and assets.  Steps:
#
#   1. stage the assets and write assets/lrg-assets.list, the manifest
#      lrg_platform_prepare() extracts from (its stamp is a hash of every
#      path and file, so a changed asset re-extracts on the device)
#   2. fill AndroidManifest.xml.in
#   3. aapt2 link: manifest + android.jar + assets -> base APK
#   4. add lib/arm64-v8a/lib<name>.so stored (not deflated): with
#      extractNativeLibs=false Android maps it straight from the APK
#   5. zipalign -P 16: uncompressed .so files on 16 KB boundaries (Android
#      15+ devices may use 16 KB pages), everything else on 4 bytes
#   6. apksigner with a debug keystore (made on first use) or --keystore
#
# Runs where the Android build tools are: the android-arm64 build container
# (`make ANDROID=1 apk' calls it), or a host with ANDROID_BUILD_TOOLS and
# ANDROID_PLATFORM_JAR set and a JDK on PATH.

set -euo pipefail

usage () {
	cat <<'USAGE'
mkapk.sh - package a libregnum game into an Android APK.

Usage: mkapk.sh --lib LIBMAIN.so --out GAME.apk [options]

Required:
  --lib FILE          the game's native library (arm64-v8a)
  --out FILE          the APK to write

Options:
  --assets DIR        directory whose contents become the APK's assets/
                      (extracted on the device by lrg_platform_prepare())
  --package ID        application id (default: org.libregnum.<basename of --out>)
  --label TEXT        launcher label (default: basename of --out)
  --version-code N    integer version (default: 1)
  --version-name S    version string (default: 1.0)
  --orientation O     landscape | portrait | sensor (default: landscape)
  --lib-name NAME     library name Android loads (default: main)
  --keystore FILE     signing keystore (default: debug.keystore next to
                      --out, created on first use; password "android")
  --ks-pass PASS      keystore password (default: android)
  --min-sdk N         minimum API (default: $ANDROID_MIN_API or 29)
  --target-sdk N      target API (default: the android.jar's, else 36)
  -h, --help          show this help
  --license           show the license

Environment (the android-arm64 container sets them in
/opt/android/versions.env, which is read when present):
  ANDROID_BUILD_TOOLS   directory with aapt2, zipalign, apksigner
  ANDROID_PLATFORM_JAR  platforms/android-<api>/android.jar

Examples:
  make ANDROID=1 apk APK_GAME=examples/game-chocolate-chip-clicker.c
  mkapk.sh --lib build/release-android-arm64/apk/clicker/libmain.so \
      --assets data --out build/release-android-arm64/apk/clicker.apk \
      --package org.example.clicker --label 'Chip Clicker'
USAGE
}

die () {
	printf 'mkapk: %s\n' "$*" >&2
	exit 1
}

lib=''
out=''
assets=''
package=''
label=''
version_code=1
version_name='1.0'
orientation='landscape'
lib_name='main'
keystore=''
ks_pass='android'
min_sdk=''
target_sdk=''

while [[ $# -gt 0 ]]
do
	case "${1}" in
		--lib) lib="${2:?--lib needs a file}"; shift 2 ;;
		--out) out="${2:?--out needs a file}"; shift 2 ;;
		--assets) assets="${2:?--assets needs a directory}"; shift 2 ;;
		--package) package="${2:?--package needs an id}"; shift 2 ;;
		--label) label="${2:?--label needs text}"; shift 2 ;;
		--version-code) version_code="${2:?--version-code needs a number}"; shift 2 ;;
		--version-name) version_name="${2:?--version-name needs a string}"; shift 2 ;;
		--orientation) orientation="${2:?--orientation needs a value}"; shift 2 ;;
		--lib-name) lib_name="${2:?--lib-name needs a name}"; shift 2 ;;
		--keystore) keystore="${2:?--keystore needs a file}"; shift 2 ;;
		--ks-pass) ks_pass="${2:?--ks-pass needs a password}"; shift 2 ;;
		--min-sdk) min_sdk="${2:?--min-sdk needs a number}"; shift 2 ;;
		--target-sdk) target_sdk="${2:?--target-sdk needs a number}"; shift 2 ;;
		-h|--help) usage; exit 0 ;;
		--license) sed -n '2,16p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'; exit 0 ;;
		*) usage >&2; exit 2 ;;
	esac
done

# --- inputs ------------------------------------------------------------------

[[ -n "${lib}" && -n "${out}" ]] || { usage >&2; exit 2; }
[[ -f "${lib}" ]] || die "no library at ${lib}"
[[ -z "${assets}" || -d "${assets}" ]] || die "no assets directory at ${assets}"
[[ "${version_code}" =~ ^[0-9]+$ ]] || die "--version-code must be an integer"
case "${orientation}" in
	landscape|portrait|sensor|sensorLandscape|sensorPortrait|fullSensor) ;;
	*) die "unknown orientation: ${orientation}" ;;
esac

if [[ -f /opt/android/versions.env ]]
then
	# shellcheck source=/dev/null
	source /opt/android/versions.env
fi
[[ -n "${ANDROID_BUILD_TOOLS:-}" && -x "${ANDROID_BUILD_TOOLS}/aapt2" ]] \
	|| die 'ANDROID_BUILD_TOOLS does not hold aapt2 (run in the android-arm64 container)'
[[ -n "${ANDROID_PLATFORM_JAR:-}" && -f "${ANDROID_PLATFORM_JAR}" ]] \
	|| die 'ANDROID_PLATFORM_JAR is not set to an android.jar'
command -v java > /dev/null || die 'apksigner needs java on PATH'

name="$(basename -- "${out}" .apk)"
package="${package:-org.libregnum.$(printf '%s' "${name}" | tr -c 'a-zA-Z0-9\n' '_')}"
label="${label:-${name}}"
min_sdk="${min_sdk:-${ANDROID_MIN_API:-29}}"
if [[ -z "${target_sdk}" ]]
then
	target_sdk="$(basename -- "$(dirname -- "${ANDROID_PLATFORM_JAR}")" | sed -n 's/^android-\([0-9][0-9]*\).*/\1/p')"
	target_sdk="${target_sdk:-36}"
fi
out_dir="$(dirname -- "${out}")"
mkdir -p -- "${out_dir}"
keystore="${keystore:-${out_dir}/debug.keystore}"
here="$(CDPATH='' cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"

work="$(mktemp -d "${TMPDIR:-/tmp}/lrg-mkapk.XXXXXX")"
trap 'rm -rf -- "${work}"' EXIT

# --- 1. assets and their manifest --------------------------------------------

mkdir -p -- "${work}/assets"
if [[ -n "${assets}" ]]
then
	cp -a -- "${assets}/." "${work}/assets/"
fi
rm -f -- "${work}/assets/lrg-assets.list"
(
	cd "${work}/assets"
	find . -type f ! -name lrg-assets.list -printf '%P\n' | LC_ALL=C sort > "${work}/asset-paths"
	# the stamp: every path and its content hash, hashed
	stamp="$(while IFS= read -r path; do printf '%s %s\n' "$(sha256sum < "${path}" | cut -c1-64)" "${path}"; done \
		< "${work}/asset-paths" | sha256sum | cut -c1-16)"
	{
		printf '# lrg-assets %s\n' "${stamp}"
		cat "${work}/asset-paths"
	} > lrg-assets.list
)
printf 'mkapk: %s assets\n' "$(wc -l < "${work}/asset-paths")"

# --- 2. manifest ---------------------------------------------------------------

sed -e "s|@PACKAGE@|${package}|g" \
    -e "s|@VERSION_CODE@|${version_code}|g" \
    -e "s|@VERSION_NAME@|${version_name}|g" \
    -e "s|@LABEL@|${label}|g" \
    -e "s|@ORIENTATION@|${orientation}|g" \
    -e "s|@LIB_NAME@|${lib_name}|g" \
    "${here}/AndroidManifest.xml.in" > "${work}/AndroidManifest.xml"

# --- 3. base APK ----------------------------------------------------------------

"${ANDROID_BUILD_TOOLS}/aapt2" link \
	-o "${work}/base.apk" \
	--manifest "${work}/AndroidManifest.xml" \
	-I "${ANDROID_PLATFORM_JAR}" \
	-A "${work}/assets" \
	--min-sdk-version "${min_sdk}" \
	--target-sdk-version "${target_sdk}" \
	--version-code "${version_code}" \
	--version-name "${version_name}" \
	|| die 'aapt2 link failed'

# --- 4. the native library, stored --------------------------------------------

mkdir -p -- "${work}/native/lib/arm64-v8a"
cp -- "${lib}" "${work}/native/lib/arm64-v8a/lib${lib_name}.so"
(cd "${work}/native" && zip -q -0 "${work}/base.apk" "lib/arm64-v8a/lib${lib_name}.so") \
	|| die 'adding the library failed'

# --- 5. alignment -----------------------------------------------------------------

"${ANDROID_BUILD_TOOLS}/zipalign" -f -P 16 4 "${work}/base.apk" "${work}/aligned.apk" \
	|| die 'zipalign failed'

# --- 6. signing -----------------------------------------------------------------

if [[ ! -f "${keystore}" ]]
then
	printf 'mkapk: creating the debug keystore %s\n' "${keystore}"
	keytool -genkeypair -noprompt -keystore "${keystore}" \
		-storepass "${ks_pass}" -keypass "${ks_pass}" -alias androiddebugkey \
		-keyalg RSA -keysize 2048 -validity 10000 \
		-dname 'CN=Android Debug,O=libregnum,C=US' > /dev/null \
		|| die 'keytool failed'
fi
"${ANDROID_BUILD_TOOLS}/apksigner" sign \
	--ks "${keystore}" --ks-pass "pass:${ks_pass}" --key-pass "pass:${ks_pass}" \
	--out "${out}" "${work}/aligned.apk" \
	|| die 'apksigner failed'
"${ANDROID_BUILD_TOOLS}/apksigner" verify "${out}" || die 'the signed APK does not verify'

printf 'mkapk: %s (%s, version %s/%s, min API %s, target API %s)\n' \
	"${out}" "${package}" "${version_name}" "${version_code}" "${min_sdk}" "${target_sdk}"
