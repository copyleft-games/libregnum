#!/bin/bash
# test-mkapk.sh - build-aux/android/mkapk.sh: asset roots, forbidden files, signing
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

# Two passes:
#
#   stubs  always, on any host: aapt2, zipalign and apksigner are scripts
#          that build a plain zip and record their argv (one argument per
#          line); checks the merged asset roots, the duplicate-path and
#          forbidden-file errors, and that a release key's password reaches
#          apksigner as env:VAR, never as text on its command line
#   real   in the android-arm64 build container (make ANDROID=1 test-mkapk):
#          the same release signing with the real build tools and a
#          throwaway PKCS12 keystore, verified by apksigner
#
# A keystore needs keytool (a JDK); without one the cases that need a real
# keystore are skipped, with the reason.

set -euo pipefail

usage () {
	cat <<'USAGE'
test-mkapk.sh - check mkapk.sh's assets and signing options.

Usage: test-mkapk.sh

Options:
  -h, --help  show this help
  --license   show the license

Examples:
  tests/test-mkapk.sh
  make test-mkapk                 # stubs, on the host
  make ANDROID=1 test-mkapk       # stubs and the real build tools
USAGE
}

case "${1:-}" in
	-h|--help)
		usage
		exit 0
		;;
	--license)
		sed -n '2,16p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'
		exit 0
		;;
esac

BS_NAME='test-mkapk'
# shellcheck source=tests/build-system-lib.sh
source "$(dirname -- "${BASH_SOURCE[0]}")/build-system-lib.sh"

MKAPK="${BS_ROOT}/build-aux/android/mkapk.sh"
SCRATCH="$(mktemp -d "${TMPDIR:-/tmp}/lrg-mkapk-test.XXXXXX")"
trap 'rm -rf -- "${SCRATCH}"' EXIT

# A password that must never show up in an argv
SECRET='lrg-test-Secret-4711'

# --- stub build tools ----------------------------------------------------------

STUBS="${SCRATCH}/stubs"
mkdir -p -- "${STUBS}"
export STUB_LOG="${SCRATCH}/tools.log"

# aapt2 link: a zip with the manifest and the assets directory
cat > "${STUBS}/aapt2" <<'STUBEOF'
#!/bin/bash
{ echo '== aapt2'; printf '%s\n' "$@"; } >> "${STUB_LOG}"
out='' manifest='' assets=''
while [[ $# -gt 0 ]]
do
	case "${1}" in
		-o) out="${2}"; shift 2 ;;
		--manifest) manifest="${2}"; shift 2 ;;
		-A) assets="${2}"; shift 2 ;;
		*) shift ;;
	esac
done
stage="$(mktemp -d)"
cp -- "${manifest}" "${stage}/AndroidManifest.xml"
cp -a -- "${assets}" "${stage}/assets"
(cd "${stage}" && zip -qr "${out}" AndroidManifest.xml assets)
rm -rf -- "${stage}"
STUBEOF

# zipalign -f -P 16 4 IN OUT: a copy
cat > "${STUBS}/zipalign" <<'STUBEOF'
#!/bin/bash
{ echo '== zipalign'; printf '%s\n' "$@"; } >> "${STUB_LOG}"
cp -- "${@: -2:1}" "${@: -1}"
STUBEOF

# apksigner sign ... --out OUT IN: records its argv, checks that every
# env:VAR it was given is set (without printing the value), copies IN
cat > "${STUBS}/apksigner" <<'STUBEOF'
#!/bin/bash
{ echo '== apksigner'; printf '%s\n' "$@"; } >> "${STUB_LOG}"
[[ "${1}" == 'sign' ]] || exit 0
out=''
for arg in "$@"
do
	if [[ "${arg}" == env:* ]]
	then
		var="${arg#env:}"
		if [[ -n "${!var:-}" ]]
		then
			echo "stub: ${var} is set" >> "${STUB_LOG}"
		else
			echo "stub: ${var} is NOT set" >> "${STUB_LOG}"
			exit 1
		fi
	fi
done
while [[ $# -gt 1 ]]
do
	[[ "${1}" == '--out' ]] && out="${2}"
	shift
done
cp -- "${1}" "${out}"
STUBEOF

# keytool for the debug key, when the host has no JDK
if ! command -v keytool > /dev/null
then
	cat > "${STUBS}/keytool" <<'STUBEOF'
#!/bin/bash
{ echo '== keytool'; printf '%s\n' "$@"; } >> "${STUB_LOG}"
while [[ $# -gt 0 ]]; do [[ "${1}" == '-keystore' ]] && : > "${2}"; shift; done
STUBEOF
fi
command -v java > /dev/null || printf '#!/bin/bash\nexit 0\n' > "${STUBS}/java"
chmod +x "${STUBS}"/*
: > "${SCRATCH}/android.jar"

# mkapk_stub ARGS... -- run mkapk.sh with the stub tools; output on stdout
mkapk_stub () {
	: > "${STUB_LOG}"
	PATH="${STUBS}:${PATH}" ANDROID_BUILD_TOOLS="${STUBS}" \
		ANDROID_PLATFORM_JAR="${SCRATCH}/android.jar" \
		bash "${MKAPK}" --lib "${SCRATCH}/libmain.so" "$@" 2>&1
}

# signer_args -- the argv of the stub's `apksigner sign'
signer_args () {
	awk '/^== / { on = ($0 == "== apksigner") ; next } on' "${STUB_LOG}" | sed -n '/^sign$/,/^== /p'
}

# A throwaway release keystore in DIR (PKCS12, alias "release");
# succeeds only with a real keytool
make_keystore () {
	local ks="${1}"

	command -v keytool > /dev/null || return 1
	[[ -x "${STUBS}/keytool" ]] && return 1
	keytool -genkeypair -noprompt -keystore "${ks}" -storetype PKCS12 \
		-storepass "${SECRET}" -keypass "${SECRET}" -alias release \
		-keyalg RSA -keysize 2048 -validity 2 \
		-dname 'CN=libregnum mkapk test,O=libregnum,C=US' > /dev/null 2>&1
}

printf 'not a real library\n' > "${SCRATCH}/libmain.so"
mkdir -p -- "${SCRATCH}/a/sub" "${SCRATCH}/b" "${SCRATCH}/c" "${SCRATCH}/d/data"
echo a > "${SCRATCH}/a/one.txt"
echo a > "${SCRATCH}/a/sub/two.bin"
echo b > "${SCRATCH}/b/three.dat"
echo c > "${SCRATCH}/c/one.txt"
echo d > "${SCRATCH}/d/data/common.MPQ"

echo '== asset roots (stubs)'
if out="$(mkapk_stub --out "${SCRATCH}/out/multi.apk" --assets "${SCRATCH}/a" --assets "${SCRATCH}/b")"
then
	bs_pass 'two roots package'
else
	bs_failed "two roots package: ${out}"
fi
listing="$(unzip -Z1 "${SCRATCH}/out/multi.apk" 2>/dev/null || true)"
for path in assets/one.txt assets/sub/two.bin assets/three.dat assets/lrg-assets.list
do
	bs_contains "merged: ${path}" "${path}" "${listing}"
done
bs_eq 'the manifest lists both roots' "$(printf 'one.txt\nsub/two.bin\nthree.dat')" \
	"$(unzip -p "${SCRATCH}/out/multi.apk" assets/lrg-assets.list | tail -n +2)"
bs_contains 'asset count' 'mkapk: 3 assets' "${out}"

if out="$(mkapk_stub --out "${SCRATCH}/out/dup.apk" --assets "${SCRATCH}/a" --assets "${SCRATCH}/c" || echo FAILED)"
bs_contains 'duplicate path fails' 'FAILED' "${out}"
bs_contains 'duplicate path named with its roots' "one.txt is in more than one assets root: ${SCRATCH}/a ${SCRATCH}/c" "${out}"
bs_eq 'duplicate path: no APK' 'no' "$([[ -e "${SCRATCH}/out/dup.apk" ]] && echo yes || echo no)"
out="$(mkapk_stub --out "${SCRATCH}/out/missing.apk" --assets "${SCRATCH}/nonexistent" || echo FAILED)"
bs_contains 'missing root fails' 'no assets directory' "${out}"

echo '== forbidden files (stubs)'
out="$(mkapk_stub --out "${SCRATCH}/out/forbid.apk" --assets "${SCRATCH}/a" --assets "${SCRATCH}/d" \
	--forbid '*.dbc' --forbid '*.MPQ' || echo FAILED)"
bs_contains 'forbidden asset fails' 'FAILED' "${out}"
bs_contains 'forbidden asset named' 'forbidden asset data/common.MPQ (matches *.MPQ)' "${out}"
bs_eq 'forbidden asset: no APK' 'no' "$([[ -e "${SCRATCH}/out/forbid.apk" ]] && echo yes || echo no)"
out="$(mkapk_stub --out "${SCRATCH}/out/allowed.apk" --assets "${SCRATCH}/a" --forbid '*.MPQ' --forbid 'secret/*')"
then
	bs_pass 'no match: packages'
else
	bs_failed "no match: packages: ${out}"
fi

echo '== release signing (stubs)'
ks="${SCRATCH}/keys/release.jks"
mkdir -p -- "${SCRATCH}/keys"
if ! make_keystore "${ks}"
then
	# the stub apksigner never opens it
	: > "${ks}"
fi
if out="$(LRG_TEST_KS_PASS="${SECRET}" mkapk_stub --out "${SCRATCH}/out/release.apk" \
	--keystore "${ks}" --ks-key-alias release --ks-pass-env LRG_TEST_KS_PASS)"
then
	bs_pass 'release signing'
else
	bs_failed "release signing: ${out}"
fi
sign="$(signer_args)"
bs_contains 'keystore password by name' "$(printf -- '--ks-pass\nenv:LRG_TEST_KS_PASS')" "${sign}"
bs_contains 'key password defaults to the same name' "$(printf -- '--key-pass\nenv:LRG_TEST_KS_PASS')" "${sign}"
bs_contains 'alias passed' "$(printf -- '--ks-key-alias\nrelease')" "${sign}"
bs_contains 'the variable reached apksigner' 'stub: LRG_TEST_KS_PASS is set' "$(cat "${STUB_LOG}")"
bs_not_contains 'the password is in no argv' "${SECRET}" "$(cat "${STUB_LOG}")"
bs_not_contains 'the password is not printed' "${SECRET}" "${out}"

if out="$(LRG_TEST_KS_PASS="${SECRET}" LRG_TEST_KEY_PASS="${SECRET}" mkapk_stub --out "${SCRATCH}/out/release2.apk" \
	--keystore "${ks}" --ks-pass-env LRG_TEST_KS_PASS --key-pass-env LRG_TEST_KEY_PASS)"
then
	bs_pass 'separate key password'
else
	bs_failed "separate key password: ${out}"
fi
bs_contains 'key password variable' "$(printf -- '--key-pass\nenv:LRG_TEST_KEY_PASS')" "$(signer_args)"
bs_not_contains 'no alias unless asked' '--ks-key-alias' "$(signer_args)"

if out="$(mkapk_stub --out "${SCRATCH}/out/unset.apk" \
	--keystore "${ks}" --ks-pass-env LRG_TEST_KS_PASS_UNSET || echo FAILED)"
bs_contains 'unset password variable fails' 'LRG_TEST_KS_PASS_UNSET is not set' "${out}"
out="$(LRG_TEST_KS_PASS="${SECRET}" mkapk_stub --out "${SCRATCH}/out/nokey.apk" \
	--keystore "${SCRATCH}/keys/missing.jks" --ks-pass-env LRG_TEST_KS_PASS || echo FAILED)"
bs_contains 'a release keystore is never created' 'no keystore at' "${out}"
bs_eq 'missing release keystore stays missing' 'no' "$([[ -e "${SCRATCH}/keys/missing.jks" ]] && echo yes || echo no)"
out="$(mkapk_stub --out "${SCRATCH}/out/keyonly.apk" --keystore "${ks}" --key-pass-env LRG_TEST_KS_PASS || echo FAILED)"
bs_contains '--key-pass-env alone is refused' 'needs --ks-pass-env' "${out}"

echo '== the debug key (stubs)'
out="$(mkapk_stub --out "${SCRATCH}/debug/game.apk")"
then
	bs_pass 'debug signing'
else
	bs_failed "debug signing: ${out}"
fi
bs_eq 'debug keystore next to the APK' 'yes' "$([[ -f "${SCRATCH}/debug/debug.keystore" ]] && echo yes || echo no)"
bs_contains 'debug key unchanged: pass:android' "$(printf -- '--ks-pass\npass:android')" "$(signer_args)"

echo '== release signing (real build tools)'
real_tools=''
if [[ -f /opt/android/versions.env ]]
then
	real_tools="$(bash -c 'source /opt/android/versions.env && printf "%s" "${ANDROID_BUILD_TOOLS:-}"')"
fi
if [[ -z "${real_tools}" || ! -x "${real_tools}/apksigner" ]]
then
	echo '  SKIP: no Android build tools here (run make ANDROID=1 test-mkapk)'
elif [[ ! -s "${ks}" ]]
then
	echo '  SKIP: no keytool to make a keystore'
else
	if out="$(LRG_TEST_KS_PASS="${SECRET}" bash "${MKAPK}" --lib "${SCRATCH}/libmain.so" \
		--out "${SCRATCH}/real/release.apk" --assets "${SCRATCH}/a" --assets "${SCRATCH}/b" \
		--keystore "${ks}" --ks-key-alias release --ks-pass-env LRG_TEST_KS_PASS 2>&1)"
	then
		bs_pass 'real release signing'
	else
		bs_failed "real release signing: ${out}"
	fi
	certs="$("${real_tools}/apksigner" verify --print-certs "${SCRATCH}/real/release.apk" 2>&1 || true)"
	bs_contains 'signed with the release key' 'CN=libregnum mkapk test' "${certs}"
	bs_contains 'real APK has the merged assets' 'assets/three.dat' "$(unzip -Z1 "${SCRATCH}/real/release.apk")"
fi

bs_finish
