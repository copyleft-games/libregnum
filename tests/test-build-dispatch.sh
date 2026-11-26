#!/bin/bash
# test-build-dispatch.sh - how cross builds are handed to the build container
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

# No container ever starts: CONTAINER_ENGINE is a stub that appends every
# invocation's argv (one argument per line, framed by "== call") to a log,
# answers `image exists' from STUB_IMAGE_EXISTS and `images' from
# STUB_IMAGES.  Each case runs the real GNUmakefile -> target.mk ->
# container.mk path and asserts on the log.

set -euo pipefail

usage () {
	cat <<'USAGE'
test-build-dispatch.sh - check the container dispatch of cross builds.

Usage: test-build-dispatch.sh

Options:
  -h, --help  show this help
  --license   show the license

Examples:
  tests/test-build-dispatch.sh
  make test-build-system
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

BS_NAME='test-build-dispatch'
# shellcheck source=tests/build-system-lib.sh
source "$(dirname -- "${BASH_SOURCE[0]}")/build-system-lib.sh"

SCRATCH="$(mktemp -d "${TMPDIR:-/tmp}/lrg-dispatch-test.XXXXXX")"
trap 'rm -rf -- "${SCRATCH}"' EXIT

STUB="${SCRATCH}/engine"
export STUB_LOG="${SCRATCH}/engine.log"
cat > "${STUB}" <<'STUBEOF'
#!/bin/bash
{
	echo '== call'
	printf '%s\n' "$@"
} >> "${STUB_LOG}"
if [[ "${1:-}" == 'image' && "${2:-}" == 'exists' ]]
then
	exit "${STUB_IMAGE_EXISTS:-0}"
fi
if [[ "${1:-}" == 'images' ]]
then
	printf '%s\n' ${STUB_IMAGES:-}
fi
exit 0
STUBEOF
chmod +x "${STUB}"

AMD=LRG_HOST_TARGET=linux-amd64

# dispatch ARGS... -- run make through GNUmakefile with the stub engine;
# prints make's output, leaves the engine's argv in ${STUB_LOG}
dispatch () {
	: > "${STUB_LOG}"
	bs_make -s -C "${BS_ROOT}" CONTAINER_ENGINE="${STUB}" "${AMD}" "$@" 2>&1
}

# calls SUBCOMMAND -- how many engine invocations began with SUBCOMMAND
calls () {
	awk -v sub_cmd="${1}" 'prev == "== call" && $0 == sub_cmd { n++ } { prev = $0 } END { print n + 0 }' "${STUB_LOG}"
}

# run_args -- the argv of the (last) `run' invocation, one per line
run_args () {
	awk '/^== call$/ { buf = ""; next } { buf = buf $0 "\n" } END { printf "%s", buf }' "${STUB_LOG}"
}

logged () {
	cat "${STUB_LOG}"
}

echo '== a cross build runs in one container'
dispatch WINDOWS=1 -j16 DEBUG=1 STEAM=1 all test > /dev/null
args="$(run_args)"
bs_eq 'one run for all goals' 1 "$(calls run)"
bs_contains 'removed after the build' '--rm' "${args}"
bs_contains 'files belong to the caller' '--userns=keep-id' "${args}"
bs_contains 'no SELinux relabelling' 'label=disable' "${args}"
bs_contains 'checkout mounted at the same path' "${BS_ROOT}:${BS_ROOT}" "${args}"
bs_contains 'runs in the same directory' "$(printf -- '-w\n%s' "${BS_ROOT}")" "${args}"
bs_contains 'marked as inside the container' 'LRG_IN_CONTAINER=1' "${args}"
bs_contains 'suffix handed in' 'LRG_BUILD_SUFFIX=-windows-amd64' "${args}"
mf="$(grep '^MAKEFLAGS=' <<< "${args}")"
bs_contains 'MAKEFLAGS carries -j16' '-j16' "${mf}"
bs_contains 'MAKEFLAGS carries DEBUG=1' 'DEBUG=1' "${mf}"
bs_contains 'MAKEFLAGS carries STEAM=1' 'STEAM=1' "${mf}"
bs_contains 'MAKEFLAGS carries the target' 'WINDOWS=1' "${mf}"
bs_not_contains 'no host jobserver' 'jobserver' "${mf}"
bs_eq 'image, make and the goals come last' \
	"$(printf 'make\nall\ntest')" "$(tail -n 3 <<< "${args}")"
bs_contains 'the image is the windows one' 'localhost/libregnum-build-windows-amd64:' "${args}"
bs_eq 'existing image is not rebuilt' 0 "$(calls build)"

echo '== goals and defaults'
dispatch WINDOWS=1 > /dev/null
bs_eq 'no goal means all' "$(printf 'make\nall')" "$(run_args | tail -n 2)"
dispatch WINDOWS=1 clean all > /dev/null
bs_eq 'mixed goals go together, in order' "$(printf 'make\nclean\nall')" "$(run_args | tail -n 3)"
dispatch WINDOWS=1 'CFLAGS_EXTRA=-DA=1 -DB=2' lib > /dev/null
bs_contains 'a value with spaces survives' 'CFLAGS_EXTRA=-DA=1\ -DB=2' "$(run_args)"

echo '== image building'
STUB_IMAGE_EXISTS=1 dispatch WINDOWS=1 lib > /dev/null
build="$(awk '/^== call$/ { n++ } n == 2' "${STUB_LOG}")"
bs_eq 'missing image is built once' 1 "$(calls build)"
bs_contains 'build uses the default base' 'BASE_IMAGE=registry.fedoraproject.org/fedora:45' "${build}"
bs_contains 'build uses the target recipe' 'build-aux/containers/windows-amd64/Containerfile' "${build}"
bs_contains 'build is labelled for prune' 'org.libregnum.build-target=windows-amd64' "${build}"
bs_eq 'build then run' 1 "$(calls run)"
STUB_IMAGE_EXISTS=1 dispatch WINDOWS=1 BUILD_CONTAINER_BASE=quay.io/fedora/fedora:44 lib > /dev/null
bs_contains 'BUILD_CONTAINER_BASE reaches the build' 'BASE_IMAGE=quay.io/fedora/fedora:44' "$(logged)"

tag_of () {
	bs_make -s -C "${BS_ROOT}" CONTAINER_ENGINE="${STUB}" "${AMD}" print-container "$@" 2>/dev/null \
		| sed -n 's/^Image: *//p'
}
default_tag="$(tag_of WINDOWS=1)"
bs_eq 'tag is stable' "${default_tag}" "$(tag_of WINDOWS=1)"
bs_not_contains 'another base, another tag' "${default_tag}" "$(tag_of WINDOWS=1 BUILD_CONTAINER_BASE=quay.io/fedora/fedora:44)"
# a copy of the build context, edited step by step
ctx="${SCRATCH}/build-aux"
mkdir -p -- "${ctx}"
cp -r -- "${BS_ROOT}/build-aux/containers" "${BS_ROOT}/build-aux/cross" "${ctx}/"
copy_tag="$(tag_of WINDOWS=1 LRG_CONTAINER_CONTEXT="${ctx}")"
bs_eq 'same recipe, same tag' "${default_tag}" "${copy_tag}"
echo '# edited' >> "${ctx}/containers/windows-amd64/Containerfile"
edited_tag="$(tag_of WINDOWS=1 LRG_CONTAINER_CONTEXT="${ctx}")"
bs_not_contains 'edited recipe, new tag' "${copy_tag}" "${edited_tag}"
: > "${ctx}/containers/common/new-helper.sh"
shared_tag="$(tag_of WINDOWS=1 LRG_CONTAINER_CONTEXT="${ctx}")"
bs_not_contains 'new shared file, new tag' "${edited_tag}" "${shared_tag}"
echo '# edited' >> "${ctx}/cross/lrg-sysroot-pkg-config"
cross_tag="$(tag_of WINDOWS=1 LRG_CONTAINER_CONTEXT="${ctx}")"
bs_not_contains 'edited cross wrapper, new tag' "${shared_tag}" "${cross_tag}"
echo '# edited' >> "${ctx}/containers/linux-arm64/setup.sh"
bs_eq "another target's recipe leaves the tag alone" "${cross_tag}" "$(tag_of WINDOWS=1 LRG_CONTAINER_CONTEXT="${ctx}")"
bs_not_contains 'each target its own tag' "${default_tag#*:}" "$(tag_of ARM64=1)"

echo '== prebuilt images and extra arguments'
dispatch WINDOWS=1 BUILD_CONTAINER_IMAGE=registry.example.org/lrg/windows:7 lib > /dev/null
bs_contains 'prebuilt image is run' 'registry.example.org/lrg/windows:7' "$(run_args)"
bs_eq 'prebuilt image is not checked' 0 "$(calls image)"
bs_eq 'prebuilt image is not built' 0 "$(calls build)"
dispatch WINDOWS=1 'BUILD_CONTAINER_ARGS=-v /srv/ccache:/ccache' lib > /dev/null
bs_contains 'extra run arguments' "$(printf -- '-v\n/srv/ccache:/ccache')" "$(run_args)"

echo '== per-target container platform'
dispatch ANDROID=1 lib > /dev/null
bs_contains 'android image is amd64' "$(printf -- '--platform\nlinux/amd64')" "$(run_args)"
bs_contains 'android image' 'localhost/libregnum-build-android-arm64:' "$(run_args)"
dispatch ARM64=1 lib > /dev/null
bs_not_contains 'arm64 image uses the host arch' '--platform' "$(run_args)"
bs_contains 'arm64 suffix' 'LRG_BUILD_SUFFIX=-arm64' "$(run_args)"

echo '== the git directory of a worktree'
common="$(cd "${BS_ROOT}" && git rev-parse --path-format=absolute --git-common-dir 2>/dev/null || true)"
dispatch WINDOWS=1 lib > /dev/null
if [[ -n "${common}" && "${common}" != "${BS_ROOT}"/* ]]
then
	bs_contains 'shared git dir mounted read-only' "${common}:${common}:ro" "$(run_args)"
else
	bs_not_contains 'checkout owns its git dir: no extra mount' ':ro' "$(run_args)"
fi

echo '== nothing is dispatched'
for goals in clean print-target targets help print-container test-device
do
	# clean only as a dry run: really running it would delete the
	# checkout's own windows build
	if [[ "${goals}" == 'clean' ]]
	then
		dispatch -n WINDOWS=1 clean > /dev/null || true
	else
		dispatch WINDOWS=1 "${goals}" > /dev/null || true
	fi
	bs_eq "WINDOWS=1 ${goals} stays on the host" 0 "$(calls run)"
done
out="$(dispatch WINDOWS=1 NO_BUILD_CONTAINER=1 print-target)"
bs_eq 'NO_BUILD_CONTAINER=1: no engine' '' "$(logged)"
bs_contains 'NO_BUILD_CONTAINER=1: host build dir' 'build/release-windows-amd64' "${out}"
out="$(dispatch WINDOWS=1 NO_BUILD_CONTAINER=1 PKG_CONFIG=/nonexistent/pkg-config lib || true)"
bs_contains 'NO_BUILD_CONTAINER=1 without a toolchain says so' 'toolchain is not installed' "${out}"
bs_eq 'native: no engine' '' "$(dispatch print-target > /dev/null; logged)"
out="$(LRG_IN_CONTAINER=1 LRG_BUILD_SUFFIX=-windows-amd64 dispatch WINDOWS=1 PKG_CONFIG=: print-target)"
bs_eq 'inside the container: no engine' '' "$(logged)"
bs_contains 'inside the container: suffix kept' 'build/release-windows-amd64' "${out}"

echo '== prune'
current="$(tag_of WINDOWS=1)"
: > "${STUB_LOG}"
STUB_IMAGES="${current} localhost/libregnum-build-windows-amd64:0123456789ab" \
	bs_make -s -C "${BS_ROOT}" CONTAINER_ENGINE="${STUB}" "${AMD}" WINDOWS=1 container-prune > /dev/null
bs_eq 'prune removes one image' 1 "$(calls rmi)"
bs_contains 'prune removes the old image' 'localhost/libregnum-build-windows-amd64:0123456789ab' "$(logged)"
bs_not_contains 'prune keeps the current image' "$(printf 'rmi\n%s' "${current}")" "$(logged)"

echo '== a cross build cannot skip the root makefile'
out="$(bs_make -s -C "${BS_ROOT}/tests" "${AMD}" WINDOWS=1 PKG_CONFIG=: all 2>&1 || true)"
bs_contains 'tests/ refuses a host cross build' 'from the top-level directory' "${out}"
out="$(bs_make -s -C "${BS_ROOT}" -f Makefile "${AMD}" WINDOWS=1 PKG_CONFIG=: lib 2>&1 || true)"
bs_contains 'make -f Makefile refuses it too' 'from the top-level directory' "${out}"

bs_finish
