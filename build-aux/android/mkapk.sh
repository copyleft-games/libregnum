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
#   1. stage the assets (one or more roots merged; the same path in two
#      roots is an error, as is any path matching a --forbid glob) and
#      write assets/lrg-assets.list, the manifest
#      lrg_platform_prepare() extracts from (its stamp is a hash of every
#      path and file, so a changed asset re-extracts on the device)
#   2. fill AndroidManifest.xml.in
#   3. aapt2 link: manifest + android.jar + assets -> base APK
#   4. add lib/arm64-v8a/lib<name>.so stored (not deflated): with
#      extractNativeLibs=false Android maps it straight from the APK
#   5. zipalign -P 16: uncompressed .so files on 16 KB boundaries (Android
#      15+ devices may use 16 KB pages), everything else on 4 bytes
#   6. apksigner with a debug keystore (made on first use) or --keystore;
#      a release key's passwords come from environment variables named by
#      --ks-pass-env/--key-pass-env (apksigner reads them itself, so they
#      never appear on a command line)
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
                      (extracted on the device by lrg_platform_prepare());
                      repeatable: the roots are merged, and a path present
                      in two of them is an error
  --forbid GLOB       fail when a staged asset's path (relative to assets/,
                      `*' also matches `/') matches GLOB; repeatable, e.g.
                      --forbid '*.MPQ' --forbid 'secret/*'
  --package ID        application id (default: org.libregnum.<basename of --out>)
  --label TEXT        launcher label (default: basename of --out)
  --version-code N    integer version (default: 1)
  --version-name S    version string (default: 1.0)
  --orientation O     landscape | portrait | sensor (default: landscape)
  --lib-name NAME     library name Android loads (default: main)
  --keystore FILE     signing keystore (default: debug.keystore next to
                      --out, created on first use; password "android").
                      With --ks-pass-env it must exist (`make keystore')
  --ks-key-alias A    the key's alias in the keystore (needed when it
                      holds more than one key)
  --ks-pass-env VAR   read the keystore password from environment
                      variable VAR (apksigner --ks-pass env:VAR)
  --key-pass-env VAR  read the key password from VAR (default: the
                      keystore password's variable)
  --ks-pass PASS      keystore password on the command line (default:
                      android; the debug key only: a command line is
                      visible to every user, use --ks-pass-env)
  --min-sdk N         minimum API (default: $ANDROID_MIN_API or 29)
  --target-sdk N      target API (default: the android.jar's, else 36)
  -h, --help          show this help
  --license           show the license

Environment (the android-arm64 container sets them in
/opt/android/versions.env, which is read when present):
  ANDROID_BUILD_TOOLS   directory with aapt2, zipalign, apksigner
  ANDROID_PLATFORM_JAR  platforms/android-<api>/android.jar
  (VAR of --ks-pass-env / --key-pass-env, e.g. APK_KEYSTORE_PASSWORD)

Examples:
  make ANDROID=1 apk APK_GAME=examples/game-chocolate-chip-clicker.c
  mkapk.sh --lib build/release-android-arm64/apk/clicker/libmain.so \
      --assets data --out build/release-android-arm64/apk/clicker.apk \
      --package org.example.clicker --label 'Chip Clicker'
  APK_KEYSTORE_PASSWORD=... mkapk.sh --lib libmain.so --out game.apk \
      --assets data --assets build/generated-data --forbid '*.MPQ' \
      --keystore ~/.local/share/libregnum-android/release.jks \
      --ks-key-alias release --ks-pass-env APK_KEYSTORE_PASSWORD
USAGE
}

die () {
	printf 'mkapk: %s\n' "$*" >&2
	exit 1
}

lib=''
out=''
assets=()
forbid=()
package=''
label=''
version_code=1
version_name='1.0'
orientation='landscape'
lib_name='main'
keystore=''
ks_pass='android'
ks_alias=''
ks_pass_env=''
key_pass_env=''
min_sdk=''
target_sdk=''

while [[ $# -gt 0 ]]
do
	case "${1}" in
		--lib) lib="${2:?--lib needs a file}"; shift 2 ;;
		--out) out="${2:?--out needs a file}"; shift 2 ;;
		--assets) assets+=("${2:?--assets needs a directory}"); shift 2 ;;
		--forbid) forbid+=("${2:?--forbid needs a glob}"); shift 2 ;;
		--package) package="${2:?--package needs an id}"; shift 2 ;;
		--label) label="${2:?--label needs text}"; shift 2 ;;
		--version-code) version_code="${2:?--version-code needs a number}"; shift 2 ;;
		--version-name) version_name="${2:?--version-name needs a string}"; shift 2 ;;
		--orientation) orientation="${2:?--orientation needs a value}"; shift 2 ;;
		--lib-name) lib_name="${2:?--lib-name needs a name}"; shift 2 ;;
		--keystore) keystore="${2:?--keystore needs a file}"; shift 2 ;;
		--ks-pass) ks_pass="${2:?--ks-pass needs a password}"; shift 2 ;;
		--ks-key-alias) ks_alias="${2:?--ks-key-alias needs an alias}"; shift 2 ;;
		--ks-pass-env) ks_pass_env="${2:?--ks-pass-env needs a variable name}"; shift 2 ;;
		--key-pass-env) key_pass_env="${2:?--key-pass-env needs a variable name}"; shift 2 ;;
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
for root in "${assets[@]}"
do
	[[ -d "${root}" ]] || die "no assets directory at ${root}"
done
# env_set NAME -- NAME is a valid variable name, set and not empty (its
# value is never printed)
env_set () {
	[[ "${1}" =~ ^[A-Za-z_][A-Za-z0-9_]*$ ]] && [[ -n "${!1:-}" ]]
}
if [[ -n "${key_pass_env}" && -z "${ks_pass_env}" ]]
then
	die '--key-pass-env needs --ks-pass-env'
fi
if [[ -n "${ks_pass_env}" ]]
then
	key_pass_env="${key_pass_env:-${ks_pass_env}}"
	[[ -n "${keystore}" ]] || die '--ks-pass-env needs --keystore'
	[[ -f "${keystore}" ]] || die "no keystore at ${keystore} (make keystore creates one)"
	env_set "${ks_pass_env}" || die "the keystore password variable ${ks_pass_env} is not set"
	env_set "${key_pass_env}" || die "the key password variable ${key_pass_env} is not set"
fi
[[ "${version_code}" =~ ^[0-9]+$ ]] || die "--version-code must be an integer"
case "${orientation}" in
	landscape|portrait|sensor|sensorLandscape|sensorPortrait|fullSensor) ;;
	*) die "unknown orientation: ${orientation}" ;;
esac

# The container's tool locations; a tool directory or android.jar given in
# the environment wins (tests/test-mkapk.sh runs stub tools in the
# container too)
if [[ -f /opt/android/versions.env ]]
then
	given_tools="${ANDROID_BUILD_TOOLS:-}"
	given_jar="${ANDROID_PLATFORM_JAR:-}"
	# shellcheck source=/dev/null
	source /opt/android/versions.env
	ANDROID_BUILD_TOOLS="${given_tools:-${ANDROID_BUILD_TOOLS:-}}"
	ANDROID_PLATFORM_JAR="${given_jar:-${ANDROID_PLATFORM_JAR:-}}"
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
# Every root's paths first: one path in two roots would silently take the
# last root's file, so it is an error naming both roots
: > "${work}/all-paths"
for root in "${assets[@]}"
do
	(cd "${root}" && find . ! -type d ! -name lrg-assets.list -printf '%P\n') \
		| while IFS= read -r path; do printf '%s\t%s\n' "${path}" "${root}"; done >> "${work}/all-paths"
done
dups="$(cut -f1 "${work}/all-paths" | LC_ALL=C sort | uniq -d)"
if [[ -n "${dups}" ]]
then
	while IFS= read -r path
	do
		printf 'mkapk: %s is in more than one assets root:%s\n' "${path}" \
			"$(awk -F '\t' -v p="${path}" '$1 == p { printf " %s", $2 }' "${work}/all-paths")" >&2
	done <<< "${dups}"
	die 'duplicate asset paths'
fi
for root in "${assets[@]}"
do
	cp -a -- "${root}/." "${work}/assets/"
done
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

# Files that must never ship (another project's proprietary data, keys):
# every match is listed, then the build stops
if [[ ${#forbid[@]} -gt 0 ]]
then
	bad=0
	while IFS= read -r path
	do
		for glob in "${forbid[@]}"
		do
			# shellcheck disable=SC2053
			if [[ "${path}" == ${glob} ]]
			then
				printf 'mkapk: forbidden asset %s (matches %s)\n' "${path}" "${glob}" >&2
				bad=1
				break
			fi
		done
	done < "${work}/asset-paths"
	[[ "${bad}" -eq 0 ]] || die 'forbidden files in the assets'
fi

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

sign_args=(--ks "${keystore}")
if [[ -n "${ks_pass_env}" ]]
then
	# apksigner reads the variables itself
	sign_args+=(--ks-pass "env:${ks_pass_env}" --key-pass "env:${key_pass_env}")
else
	sign_args+=(--ks-pass "pass:${ks_pass}" --key-pass "pass:${ks_pass}")
fi
if [[ -n "${ks_alias}" ]]
then
	sign_args+=(--ks-key-alias "${ks_alias}")
fi
if [[ ! -f "${keystore}" ]]
then
	printf 'mkapk: creating the debug keystore %s\n' "${keystore}"
	keytool -genkeypair -noprompt -keystore "${keystore}" \
		-storepass "${ks_pass}" -keypass "${ks_pass}" -alias androiddebugkey \
		-keyalg RSA -keysize 2048 -validity 10000 \
		-dname 'CN=Android Debug,O=libregnum,C=US' > /dev/null \
		|| die 'keytool failed'
fi
"${ANDROID_BUILD_TOOLS}/apksigner" sign "${sign_args[@]}" \
	--out "${out}" "${work}/aligned.apk" \
	|| die 'apksigner failed'
"${ANDROID_BUILD_TOOLS}/apksigner" verify "${out}" || die 'the signed APK does not verify'

printf 'mkapk: %s (%s, version %s/%s, min API %s, target API %s)\n' \
	"${out}" "${package}" "${version_name}" "${version_code}" "${min_sdk}" "${target_sdk}"
