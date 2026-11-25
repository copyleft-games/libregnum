#!/bin/bash
# test-shader-glsl.sh - validate every built-in shader as GLSL 330 and ES 300
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

# The engine's shaders are C string literals that start with
# LRG_GLSL_HEADER (src/graphics/lrg-glsl-private.h).  This script:
#
#   1. finds every source file that uses LRG_GLSL_HEADER,
#   2. runs the C preprocessor over it twice with the build's own flags:
#      as is (desktop, "#version 330") and with -DLRG_GLSL_ES=1 (the
#      Android header, "#version 300 es" + precisions),
#   3. pulls out every string-literal sequence that starts with "#version"
#      and contains "void main" (complete shaders; the pieces the shader
#      asset assembles at run time are covered by its own tests),
#   4. compiles each with glslangValidator as the stage its C array names
#      (*_vs, *vert* or *_fs, *frag*), or else as a vertex shader when it
#      writes gl_Position and a fragment shader otherwise.
#
# A shader that is valid GLSL 3.30 but not GLSL ES 3.00 (an implicit int
# to float conversion, a double, a sampler without precision) fails here,
# on the desktop, before any device sees it.
#
# Needs: the C compiler of the build (CC), glslangValidator (Fedora:
# glslang; the android-arm64 build container has it).
#
# Environment (set by `make test-shaders'):
#   CC                   the compiler used as preprocessor
#   LRG_SHADER_CFLAGS    the library's compile flags (LIB_CFLAGS)

set -euo pipefail

usage () {
	cat <<'USAGE'
test-shader-glsl.sh - validate the built-in shaders as GLSL 330 and GLSL ES 300.

Usage: test-shader-glsl.sh [-k] [ROOT]

Options:
  -k, --keep  keep the extracted shaders (prints where)
  -h, --help  show this help
  --license   show the license

Examples:
  make test-shaders              # native, needs glslangValidator on the host
  make ANDROID=1 test-shaders    # in the android build container
USAGE
}

keep=0
root="$(CDPATH='' cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
while [[ $# -gt 0 ]]
do
	case "${1}" in
		-k|--keep)
			keep=1
			shift
			;;
		-h|--help)
			usage
			exit 0
			;;
		--license)
			sed -n '2,16p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'
			exit 0
			;;
		-*)
			usage >&2
			exit 2
			;;
		*)
			root="$(CDPATH='' cd -- "${1}" && pwd)"
			shift
			;;
	esac
done

if ! command -v glslangValidator > /dev/null 2>&1
then
	echo 'test-shader-glsl: glslangValidator is required (Fedora: glslang; or run make ANDROID=1 test-shaders)' >&2
	exit 2
fi

cc="${CC:-gcc}"
read -r -a cflags <<< "${LRG_SHADER_CFLAGS:-}"
work="$(mktemp -d "${TMPDIR:-/tmp}/lrg-shader-glsl.XXXXXX")"
if [[ "${keep}" -eq 0 ]]
then
	trap 'rm -rf -- "${work}"' EXIT
fi

# extract PREPROCESSED OUTDIR PREFIX -- write each complete shader in the
# preprocessed C file to OUTDIR/PREFIX-<n>.vert|frag; prints the count
extract () {
	if [[ $# -ne 3 ]]
	then
		echo 'extract() requires PREPROCESSED OUTDIR PREFIX' >&2
		return 2
	fi

	gawk -v outdir="${2}" -v prefix="${3}" '
		# the decoded text of every "..." literal on a line, in order
		function literals(line,    out, rest, m) {
			out = ""
			rest = line
			while (match(rest, /"([^"\\]|\\.)*"/)) {
				m = substr(rest, RSTART + 1, RLENGTH - 2)
				gsub(/\\n/, "\n", m)
				gsub(/\\t/, "\t", m)
				gsub(/\\"/, "\"", m)
				gsub(/\\\\/, "\\", m)
				out = out m
				rest = substr(rest, RSTART + RLENGTH)
			}
			return out
		}
		# the stage: from the C variable name (*_vs / *vert*, *_fs /
		# *frag*), else a vertex shader is one that writes gl_Position (a
		# transform-feedback vertex shader does not, hence the name first)
		function stage_of(name, src) {
			if (name ~ /(_vs|vert)/)
				return "vert"
			if (name ~ /(_fs|frag)/)
				return "frag"
			return (src ~ /gl_Position/) ? "vert" : "frag"
		}
		function flush(    stage, file) {
			if (text ~ /void[ \t]+main/) {
				stage = stage_of(name, text)
				file = sprintf("%s/%s-%d.%s", outdir, prefix, ++count, stage)
				printf "%s", text > file
				close(file)
			}
			text = ""
			name = ""
			collecting = 0
		}
		/^#/ { next }
		{
			# remember the array the next literal initialises
			if (!collecting && match($0, /[A-Za-z_][A-Za-z0-9_]*\[\][ \t]*=/)) {
				name = substr($0, RSTART, RLENGTH)
				sub(/\[.*/, "", name)
			}
			if (!collecting && match($0, /"#version /)) {
				collecting = 1
				text = ""
				$0 = substr($0, RSTART)
			}
			if (collecting) {
				text = text literals($0)
				# the literal sequence ends at the first line with
				# something after it (; , or a closing paren)
				tail = $0
				gsub(/"([^"\\]|\\.)*"/, "", tail)
				if (tail ~ /[;,)]/)
					flush()
			}
		}
		END { if (collecting) flush(); print count + 0 }
	' "${1}"
}

failures=0
checked=0
mapfile -t sources < <(cd "${root}" && grep -rl --include='*.c' 'LRG_GLSL_HEADER' src | sort)
if [[ ${#sources[@]} -eq 0 ]]
then
	echo 'test-shader-glsl: no source uses LRG_GLSL_HEADER' >&2
	exit 1
fi

for source in "${sources[@]}"
do
	name="$(basename -- "${source}" .c)"
	for dialect in desktop es
	do
		dir="${work}/${dialect}"
		mkdir -p -- "${dir}"
		extra=()
		if [[ "${dialect}" == 'es' ]]
		then
			extra=(-DLRG_GLSL_ES=1)
		fi
		if ! (cd "${root}" && "${cc}" -E -P "${cflags[@]}" "${extra[@]}" -I"${root}/src" \
			"${source}" -o "${dir}/${name}.i") 2> "${dir}/${name}.cpp-err"
		then
			echo "  FAIL  ${source}: preprocessing failed" >&2
			sed 's/^/        /' "${dir}/${name}.cpp-err" | head -5 >&2
			failures=$((failures + 1))
			continue
		fi
		count="$(extract "${dir}/${name}.i" "${dir}" "${name}")"
		if [[ "${count}" -eq 0 ]]
		then
			continue
		fi
		for shader in "${dir}/${name}"-*.vert "${dir}/${name}"-*.frag
		do
			[[ -f "${shader}" ]] || continue
			checked=$((checked + 1))
			if out="$(glslangValidator "${shader}" 2>&1)"
			then
				printf '  ok    %s %s\n' "${dialect}" "$(basename -- "${shader}")"
			else
				failures=$((failures + 1))
				printf '  FAIL  %s %s (%s)\n' "${dialect}" "$(basename -- "${shader}")" "${source}" >&2
				grep -E 'ERROR' <<< "${out}" | head -5 | sed 's/^/        /' >&2
			fi
		done
	done
done

printf 'test-shader-glsl: %d shaders checked, %d failed\n' "${checked}" "${failures}"
if [[ "${keep}" -eq 1 ]]
then
	printf 'kept: %s\n' "${work}"
fi
if [[ "${checked}" -eq 0 ]]
then
	echo 'test-shader-glsl: no complete shader found (extraction broken?)' >&2
	exit 1
fi
[[ "${failures}" -eq 0 ]]
