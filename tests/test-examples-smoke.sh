#!/bin/bash
# test-examples-smoke.sh - run the headless examples small and check their output
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
#
# Usage: test-examples-smoke.sh <examples-build-dir>
#   e.g. tests/test-examples-smoke.sh examples/build/release/examples
#
# Each case runs one example that renders without a display into a fresh
# temp directory (small size, no video), then checks every file it must
# write exists and is not empty.  DISPLAY and WAYLAND_DISPLAY are unset so
# nothing can open on the developer's desktop.  Prints TAP.

set -o nounset
set -o pipefail

# name | arguments (OUT is replaced by the temp dir) | files that must exist
CASES=(
	'reel-captions-demo|--out OUT --scale 0.25 --no-video|reel-captions-demo-day.png reel-captions-demo-dissolve.png reel-captions-demo-night.png'
	'skeleton2d-talking-head|--out OUT --scale 0.25 --no-video|frames/frame_00000.png frames/frame_00050.png'
	'skeleton2d-shape-sheet|--out OUT --cell 64|shape-sheet.png'
	'cookbook|--out OUT --size 160x100 --pitfalls|cookbook-aa-shapes.png cookbook-sdf-character-bust.png cookbook-speech-bubble-layout.png cookbook-particles-glow.png'
	'image-quality-showcase|--out OUT --size 320x160|image-quality-showcase.png'
	'sdf-portrait|--out OUT --size 80x100 --samples 1|sdf-portrait.png'
	'frame-layout-poster|--out OUT --size 480x270 --debug-boxes|frame-layout-poster.png frame-layout-poster-debug.png'
)

run_case () {
	if [[ $# -ne 3 ]]
	then
		echo "run_case() requires 3 positional arguments"
		echo 'run_case <bin-dir> <index> <case>'
		exit 1
	fi

	local bin_dir="${1}"
	local index="${2}"
	local spec="${3}"
	local name
	local args
	local files
	local out
	local file
	local log
	local -a argv

	IFS='|' read -r name args files <<< "${spec}"
	out="$(mktemp -d "${TMPDIR:-/tmp}/lrg-example-smoke-XXXXXX")"
	log="${out}/run.log"

	if [[ ! -x "${bin_dir}/${name}" ]]
	then
		echo "not ok ${index} ${name} - not built (${bin_dir}/${name})"
		rm -r -- "${out}"
		return 1
	fi

	read -r -a argv <<< "${args//OUT/${out}}"
	if ! env -u DISPLAY -u WAYLAND_DISPLAY LRG_EXTERNAL_EDITOR=none \
		"${bin_dir}/${name}" "${argv[@]}" > "${log}" 2>&1
	then
		echo "not ok ${index} ${name} - exited non-zero"
		sed 's/^/# /' "${log}"
		rm -r -- "${out}"
		return 1
	fi

	for file in ${files}
	do
		if [[ ! -s "${out}/${file}" ]]
		then
			echo "not ok ${index} ${name} - missing or empty ${file}"
			rm -r -- "${out}"
			return 1
		fi
	done

	echo "ok ${index} ${name}"
	rm -r -- "${out}"
	return 0
}

main () {
	if [[ $# -ne 1 ]]
	then
		echo "main() requires 1 positional argument"
		echo 'test-examples-smoke.sh <examples-build-dir>'
		exit 1
	fi

	local bin_dir="${1}"
	local failed=0
	local i

	echo "1..${#CASES[@]}"
	for i in "${!CASES[@]}"
	do
		run_case "${bin_dir}" "$((i + 1))" "${CASES[${i}]}" || failed=1
	done
	exit "${failed}"
}

main "$@"
