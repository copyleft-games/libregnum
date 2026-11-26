#!/bin/bash
# test-check-glsl.sh - tests for build-aux/check-glsl.sh
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

# Writes good and bad sample shaders to a scratch directory and checks
# what check-glsl.sh makes of them.  The parts that need no compiler
# always run (the headers match src/graphics/lrg-glsl-private.h, usage
# errors, a missing glslangValidator is exit 2); the compile cases are
# skipped when glslangValidator is not installed.

set -euo pipefail

usage () {
	cat <<'USAGE'
test-check-glsl.sh - tests for build-aux/check-glsl.sh.

Usage: test-check-glsl.sh [ROOT]

Options:
  -h, --help  show this help
  --license   show the license

Examples:
  bash tests/test-check-glsl.sh
  make -C tests check-glsl-script
USAGE
}

root="$(CDPATH='' cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
case "${1:-}" in
	-h|--help)
		usage
		exit 0
		;;
	--license)
		sed -n '2,16p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'
		exit 0
		;;
	'')
		;;
	*)
		root="$(CDPATH='' cd -- "${1}" && pwd)"
		;;
esac

script="${root}/build-aux/check-glsl.sh"
work="$(mktemp -d "${TMPDIR:-/tmp}/lrg-test-check-glsl.XXXXXX")"
trap 'rm -rf -- "${work}"' EXIT

passed=0
failed=0

# ok NAME -- record a passing case
ok () {
	if [[ $# -ne 1 ]]
	then
		echo 'ok() requires NAME' >&2
		exit 2
	fi

	passed=$((passed + 1))
	printf '  ok    %s\n' "${1}"
}

# fail NAME -- record a failing case
fail () {
	if [[ $# -ne 1 ]]
	then
		echo 'fail() requires NAME' >&2
		exit 2
	fi

	failed=$((failed + 1))
	printf '  FAIL  %s\n' "${1}" >&2
}

# expect_status NAME WANT CMD... -- run CMD, compare its exit status
expect_status () {
	if [[ $# -lt 3 ]]
	then
		echo 'expect_status() requires NAME WANT CMD...' >&2
		exit 2
	fi

	local name="${1}"
	local want="${2}"
	local got=0

	shift 2
	"$@" > "${work}/out" 2>&1 || got=$?
	if [[ "${got}" -eq "${want}" ]]
	then
		ok "${name}"
	else
		fail "${name} (exit ${got}, wanted ${want})"
		sed 's/^/        /' "${work}/out" >&2
	fi
}

# c_header NAME -- the string literal lines of #define NAME in the C
# header, decoded (the C side of the one header text)
c_header () {
	if [[ $# -ne 1 ]]
	then
		echo 'c_header() requires NAME' >&2
		exit 2
	fi

	gawk -v name="${1}" '
		$0 ~ "^#define " name "( |$)" { on = 1 }
		on {
			line = $0
			while (match(line, /"([^"\\]|\\.)*"/)) {
				s = substr(line, RSTART + 1, RLENGTH - 2)
				gsub(/\\n/, "\n", s)
				printf "%s", s
				line = substr(line, RSTART + RLENGTH)
			}
			if ($0 !~ /\\$/)
				exit
		}
	' "${root}/src/graphics/lrg-glsl-private.h"
}

# --- no compiler needed ---------------------------------------------------

for dialect in desktop es
do
	if [[ "${dialect}" == 'es' ]]
	then
		macro='LRG_GLSL_HEADER_ES'
	else
		macro='LRG_GLSL_HEADER_DESKTOP'
	fi
	if [[ "$(bash "${script}" --print-header "${dialect}"; echo x)" == "$(c_header "${macro}"; echo x)" ]]
	then
		ok "the ${dialect} header matches ${macro}"
	else
		fail "the ${dialect} header differs from ${macro}"
	fi
done

expect_status 'no input is a usage error' 2 bash "${script}"
expect_status 'an unknown option is a usage error' 2 bash "${script}" --frobnicate x.vs
expect_status 'an unknown dialect is a usage error' 2 bash "${script}" --dialect gles2 x.vs
expect_status '--help' 0 bash "${script}" --help
expect_status '--license' 0 bash "${script}" --license

# without glslangValidator on PATH the script says so and exits 2
mkdir -p "${work}/nobin"
for tool in bash gawk sed grep find sort mktemp rm mkdir cat head
do
	if command -v "${tool}" > /dev/null 2>&1
	then
		ln -s "$(command -v "${tool}")" "${work}/nobin/${tool}"
	fi
done
printf 'void main() {}\n' > "${work}/lonely.frag"
expect_status 'a missing glslangValidator is exit 2' 2 \
	env PATH="${work}/nobin" bash "${script}" "${work}/lonely.frag"

# --- sample shaders ---------------------------------------------------------

if ! command -v glslangValidator > /dev/null 2>&1
then
	printf '  skip  compile cases (glslangValidator not installed; Fedora: glslang)\n'
	printf 'test-check-glsl: %d passed, %d failed\n' "${passed}" "${failed}"
	[[ "${failed}" -eq 0 ]]
	exit
fi

good="${work}/good"
bad="${work}/bad"
mkdir -p "${good}" "${bad}"

# a legacy desktop shader: comments, then #version 330 (replaced)
cat > "${good}/legacy.vs.glsl" <<'GLSL'
// legacy desktop shader
/* a block
   comment */
#version 330
in vec3 vertexPosition;
uniform mat4 mvp;
void main() { gl_Position = mvp * vec4(vertexPosition, 1.0); }
GLSL

# a body without #version, a sampler2DArray (needs the ES precision)
cat > "${good}/layers.frag" <<'GLSL'
in vec2 fragTexCoord;
uniform sampler2DArray layers;
out vec4 finalColor;
void main() { finalColor = texture(layers, vec3(fragTexCoord, 0.0)); }
GLSL

# no stage in the name: writes gl_Position, so a vertex shader
cat > "${good}/fullscreen.glsl" <<'GLSL'
in vec3 vertexPosition;
void main() { gl_Position = vec4(vertexPosition, 1.0); }
GLSL

# an include piece: no main, skipped
cat > "${good}/common.glsl" <<'GLSL'
vec3 lrg_double(vec3 v) { return v * 2.0; }
GLSL

# a define the shader needs (--define)
cat > "${good}/tinted.fs" <<'GLSL'
out vec4 finalColor;
void main() { finalColor = vec4(TINT); }
GLSL

# valid GLSL 3.30, invalid GLSL ES 3.00: int to float
cat > "${bad}/implicit.frag" <<'GLSL'
#version 330
out vec4 finalColor;
void main() { float x = 1; finalColor = vec4(x); }
GLSL

expect_status 'good shaders pass in both dialects' 0 \
	bash "${script}" -D '#define TINT 0.5' "${good}"
if grep -q 'skip.*common.glsl (no main' "${work}/out"
then
	ok 'an include piece is skipped'
else
	fail 'an include piece is skipped'
fi
if grep -q 'ok    es      .*fullscreen.glsl (vert)' "${work}/out"
then
	ok 'gl_Position makes an unnamed file a vertex shader'
else
	fail 'gl_Position makes an unnamed file a vertex shader'
fi
if grep -q 'check-glsl: 8 checked, 1 skipped, 0 failed' "${work}/out"
then
	ok 'the summary counts every dialect'
else
	fail 'the summary counts every dialect'
	sed 's/^/        /' "${work}/out" >&2
fi

expect_status '--stage-from-name skips an unnamed file' 0 \
	bash "${script}" --stage-from-name -D '#define TINT 0.5' "${good}"
if grep -q 'skip.*fullscreen.glsl (no stage in the name)' "${work}/out"
then
	ok '--stage-from-name reports the skip'
else
	fail '--stage-from-name reports the skip'
fi

expect_status 'a missing define fails' 1 bash "${script}" "${good}/tinted.fs"
expect_status '--skip leaves the failing file out' 0 \
	bash "${script}" --skip 'tinted*' "${good}"

expect_status 'desktop accepts the implicit conversion' 0 \
	bash "${script}" --dialect desktop "${bad}/implicit.frag"
expect_status 'ES rejects the implicit conversion' 1 \
	bash "${script}" --dialect es "${bad}/implicit.frag"
expect_status 'both dialects fail when one does' 1 bash "${script}" "${bad}"
if grep -q 'FAIL  es      .*implicit.frag' "${work}/out" && ! grep -q 'FAIL  desktop' "${work}/out"
then
	ok 'the failure names the dialect'
else
	fail 'the failure names the dialect'
fi

expect_status 'only include pieces: nothing checked is a failure' 1 \
	bash "${script}" "${good}/common.glsl"

printf 'test-check-glsl: %d passed, %d failed\n' "${passed}" "${failed}"
[[ "${failed}" -eq 0 ]]
