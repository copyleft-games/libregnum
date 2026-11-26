#!/bin/bash
# check-glsl.sh - validate shader files as GLSL 3.30 and GLSL ES 3.00
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

# A shader written for libregnum's lrg_glsl_prepare_source() is a body in
# the common GLSL 3.30 / ES 3.00 subset; at run time the platform header
# replaces its leading "#version" line.  This script does the same on
# the desktop, for both dialects, and compiles the result with
# glslangValidator, so a shader that only works on desktop GL (an
# implicit int to float conversion, a double, a sampler without
# precision) fails here, before a phone sees it.
#
# For every file (directories are searched for shader files):
#
#   1. the stage comes from the name: *.vs *.vert *.vsh (also with a
#      trailing .glsl, and *_vs.glsl *_vert.glsl) are vertex shaders,
#      *.fs *.frag *.fsh (likewise) fragment shaders; another name is a
#      vertex shader when it writes gl_Position, else a fragment shader
#      (with --stage-from-name such a file is skipped instead),
#   2. a file without "void main" is an include piece and is skipped,
#      and so is a name matching a --skip GLOB,
#   3. a leading "#version" line is blanked (its line kept), the dialect
#      header (the text of lrg_glsl_get_header(), with --define lines
#      after it) is put in front, and glslangValidator compiles it.
#
# The two headers below are the ones in src/graphics/lrg-glsl-private.h
# (tests/test-check-glsl.sh keeps them equal).
#
# Needs: bash 5, gawk, glslangValidator (Fedora: glslang; the
# android-arm64 build container has it).  Exit status: 0 when every
# shader compiled in every dialect, 1 when one failed or none was found,
# 2 for a usage error or a missing glslangValidator.

set -euo pipefail

usage () {
	cat <<'USAGE'
check-glsl.sh - validate shader files as GLSL 3.30 and GLSL ES 3.00.

Usage: check-glsl.sh [OPTIONS] FILE|DIR...

Options:
  --stage-from-name     only take the stage from the file name; skip files
                        whose name does not give one
  --skip GLOB           skip files whose base name matches GLOB (repeatable)
  --dialect D           desktop, es or both (default: both)
  -D, --define TEXT     a line put after the header, e.g. '#define LIT 1'
                        (repeatable)
  --print-header D      print the desktop or es header and exit
  -k, --keep            keep the prepared shaders (prints where)
  -q, --quiet           print only failures and the summary
  -h, --help            show this help
  --license             show the license

Stages by name: .vs .vert .vsh (vertex), .fs .frag .fsh (fragment), each
also with a trailing .glsl, plus *_vs.glsl, *_vert.glsl, *_fs.glsl and
*_frag.glsl.  A directory is searched for those and for *.glsl.

Examples:
  check-glsl.sh data/shaders
  check-glsl.sh --stage-from-name --skip 'common*' data/shaders
  check-glsl.sh --dialect es water.fs.glsl water.vs.glsl
  check-glsl.sh -D '#define LRG_LIT 1' lit.frag
  podman run --rm -v "$PWD:/w:z" -w /w IMAGE deps/libregnum/build-aux/check-glsl.sh data/shaders
USAGE
}

HEADER_DESKTOP=$'#version 330\n'
HEADER_ES=$'#version 300 es\nprecision highp float;\nprecision highp int;\nprecision highp sampler2D;\nprecision highp samplerCube;\nprecision highp sampler3D;\nprecision highp sampler2DShadow;\nprecision highp sampler2DArray;\n'

# stage_from_name FILE -- vert, frag or nothing
stage_from_name () {
	if [[ $# -ne 1 ]]
	then
		echo 'stage_from_name() requires FILE' >&2
		return 2
	fi

	local base="${1##*/}"

	base="${base%.glsl}"
	case "${base}" in
		*.vs|*.vert|*.vsh|*_vs|*_vert)
			echo vert
			;;
		*.fs|*.frag|*.fsh|*_fs|*_frag)
			echo frag
			;;
		*)
			;;
	esac
}

# is_shader_name FILE -- whether a directory walk picks FILE up
is_shader_name () {
	if [[ $# -ne 1 ]]
	then
		echo 'is_shader_name() requires FILE' >&2
		return 2
	fi

	case "${1##*/}" in
		*.vs|*.vert|*.vsh|*.fs|*.frag|*.fsh|*.glsl)
			return 0
			;;
		*)
			return 1
			;;
	esac
}

# strip_version FILE -- FILE with its leading #version directive blanked
# (only whitespace and comments may precede it, as in GLSL; the line
# itself stays, so the compiler's line numbers still match the file)
strip_version () {
	if [[ $# -ne 1 ]]
	then
		echo 'strip_version() requires FILE' >&2
		return 2
	fi

	gawk '
		BEGIN { done = 0; in_comment = 0 }
		done { print; next }
		{
			line = $0
			rest = line
			# drop the comments on this line to see what code it has
			for (;;) {
				if (in_comment) {
					end = index(rest, "*/")
					if (end == 0) { rest = ""; break }
					rest = substr(rest, end + 2)
					in_comment = 0
					continue
				}
				sub(/^[ \t\r\f\v]+/, "", rest)
				if (substr(rest, 1, 2) == "//") { rest = ""; break }
				if (substr(rest, 1, 2) == "/*") {
					rest = substr(rest, 3)
					in_comment = 1
					continue
				}
				break
			}
			if (rest == "") { print line; next }
			if (rest ~ /^#[ \t]*version([ \t]|$)/) {
				# keep what precedes the directive on its line
				print substr(line, 1, length(line) - length(rest))
			} else {
				print line
			}
			done = 1
		}
	' "${1}"
}

stage_only=0
dialect='both'
keep=0
quiet=0
skips=()
defines=()
inputs=()

while [[ $# -gt 0 ]]
do
	case "${1}" in
		--stage-from-name)
			stage_only=1
			shift
			;;
		--skip)
			[[ $# -ge 2 ]] || { usage >&2; exit 2; }
			skips+=("${2}")
			shift 2
			;;
		--dialect)
			[[ $# -ge 2 ]] || { usage >&2; exit 2; }
			dialect="${2}"
			shift 2
			;;
		-D|--define)
			[[ $# -ge 2 ]] || { usage >&2; exit 2; }
			defines+=("${2}")
			shift 2
			;;
		--print-header)
			[[ $# -ge 2 ]] || { usage >&2; exit 2; }
			case "${2}" in
				desktop) printf '%s' "${HEADER_DESKTOP}" ;;
				es) printf '%s' "${HEADER_ES}" ;;
				*) usage >&2; exit 2 ;;
			esac
			exit 0
			;;
		-k|--keep)
			keep=1
			shift
			;;
		-q|--quiet)
			quiet=1
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
		--)
			shift
			inputs+=("$@")
			break
			;;
		-*)
			usage >&2
			exit 2
			;;
		*)
			inputs+=("${1}")
			shift
			;;
	esac
done

case "${dialect}" in
	desktop) dialects=(desktop) ;;
	es) dialects=(es) ;;
	both) dialects=(desktop es) ;;
	*) usage >&2; exit 2 ;;
esac

if [[ ${#inputs[@]} -eq 0 ]]
then
	usage >&2
	exit 2
fi

if ! command -v glslangValidator > /dev/null 2>&1
then
	echo 'check-glsl: glslangValidator is required (Fedora: glslang; the android-arm64 build container has it)' >&2
	exit 2
fi

# the files to check, in a stable order
files=()
for input in "${inputs[@]}"
do
	if [[ -d "${input}" ]]
	then
		while IFS= read -r -d '' found
		do
			if is_shader_name "${found}"
			then
				files+=("${found}")
			fi
		done < <(find "${input}" -type f -print0 | sort -z)
	elif [[ -f "${input}" ]]
	then
		files+=("${input}")
	else
		echo "check-glsl: no such file or directory: ${input}" >&2
		exit 2
	fi
done

work="$(mktemp -d "${TMPDIR:-/tmp}/lrg-check-glsl.XXXXXX")"
if [[ "${keep}" -eq 0 ]]
then
	trap 'rm -rf -- "${work}"' EXIT
fi

define_text=''
for define in "${defines[@]}"
do
	define_text+="${define}"$'\n'
done

checked=0
skipped=0
failures=0
index=0

for file in "${files[@]}"
do
	base="${file##*/}"

	# 1. --skip globs
	skip=0
	for glob in "${skips[@]}"
	do
		# shellcheck disable=SC2053  # the glob is meant to match
		if [[ "${base}" == ${glob} ]]
		then
			skip=1
			break
		fi
	done
	if [[ "${skip}" -eq 1 ]]
	then
		skipped=$((skipped + 1))
		[[ "${quiet}" -eq 1 ]] || printf '  skip  %s (--skip)\n' "${file}"
		continue
	fi

	# 2. include pieces have no entry point
	if ! grep -Eq 'void[[:space:]]+main[[:space:]]*\(' -- "${file}"
	then
		skipped=$((skipped + 1))
		[[ "${quiet}" -eq 1 ]] || printf '  skip  %s (no main: include piece)\n' "${file}"
		continue
	fi

	# 3. the stage
	stage="$(stage_from_name "${file}")"
	if [[ -z "${stage}" ]]
	then
		if [[ "${stage_only}" -eq 1 ]]
		then
			skipped=$((skipped + 1))
			[[ "${quiet}" -eq 1 ]] || printf '  skip  %s (no stage in the name)\n' "${file}"
			continue
		fi
		if grep -q 'gl_Position' -- "${file}"
		then
			stage='vert'
		else
			stage='frag'
		fi
	fi

	# 4. every dialect
	index=$((index + 1))
	body="$(strip_version "${file}")"
	for d in "${dialects[@]}"
	do
		if [[ "${d}" == 'es' ]]
		then
			header="${HEADER_ES}"
		else
			header="${HEADER_DESKTOP}"
		fi
		out_file="${work}/${d}/${index}-${base%.*}.${stage}"
		mkdir -p -- "${work}/${d}"
		printf '%s%s%s\n' "${header}" "${define_text}" "${body}" > "${out_file}"
		checked=$((checked + 1))
		if out="$(glslangValidator "${out_file}" 2>&1)"
		then
			[[ "${quiet}" -eq 1 ]] || printf '  ok    %-7s %s (%s)\n' "${d}" "${file}" "${stage}"
		else
			failures=$((failures + 1))
			printf '  FAIL  %-7s %s (%s)\n' "${d}" "${file}" "${stage}" >&2
			grep -E 'ERROR' <<< "${out}" | head -5 | sed 's/^/        /' >&2 || true
		fi
	done
done

printf 'check-glsl: %d checked, %d skipped, %d failed\n' "${checked}" "${skipped}" "${failures}"
if [[ "${keep}" -eq 1 ]]
then
	printf 'kept: %s\n' "${work}"
fi
if [[ "${checked}" -eq 0 ]]
then
	echo 'check-glsl: no shader to check' >&2
	exit 1
fi
[[ "${failures}" -eq 0 ]]
