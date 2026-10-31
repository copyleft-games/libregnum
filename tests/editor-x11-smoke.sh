#!/bin/bash
# libregnum - editor X11 smoke test
# Copyright (C) 2025  Zach Podbielniak
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
# Drives a real lrgeditor window with real X input events (Xvfb + xdotool)
# and checks the results over the editor's IPC socket. The headless app
# tests inject input straight into the GUI; this one goes through raylib
# and GLFW, which is where a menubar that never opened and a viewport that
# never rotated came from.
#
# Needs: Xvfb, xdotool, socat, yq. Skips (exit 0) when any is missing.

set -euo pipefail

usage () {
	echo 'editor-x11-smoke.sh - drive lrgeditor with real X events and check it over IPC'
	echo ''
	echo 'Usage: editor-x11-smoke.sh [-h|--help] [--license] [LRG_EDITOR_BINARY]'
	echo ''
	echo 'Examples:'
	echo '  tests/editor-x11-smoke.sh build/release/lrgeditor'
	echo '  make test-editor-x11'
}

case "${1:-}" in
	-h|--help)
		usage
		exit 0
		;;
	--license)
		sed -n '2,15p' "${0}" | sed 's/^# \{0,1\}//'
		exit 0
		;;
esac

editor="${1:-build/release/lrgeditor}"
display_num=":${LRG_SMOKE_DISPLAY:-97}"

for tool in Xvfb xdotool socat yq
do
	if ! command -v "${tool}" >/dev/null 2>&1
	then
		echo "editor-x11-smoke: ${tool} not found; skipping"
		exit 0
	fi
done
if [[ ! -x "${editor}" ]]
then
	echo "editor-x11-smoke: ${editor} is not executable" >&2
	exit 1
fi

# Short paths: Unix sockets are limited to 107 bytes. A private runtime dir
# also keeps GLFW from finding (and opening a window on) a Wayland session.
runtime="${XDG_RUNTIME_DIR:-/tmp}"
work="$(mktemp -d "${runtime}/lrg-smoke.XXXXXX")"
chmod 700 "${work}"
socket="${work}/ed.sock"
xvfb_pid=''
editor_pid=''
failures=0

cleanup () {
	[[ -n "${editor_pid}" ]] && kill "${editor_pid}" 2>/dev/null || true
	[[ -n "${xvfb_pid}" ]] && kill "${xvfb_pid}" 2>/dev/null || true
	rm -rf -- "${work}"
}
trap cleanup EXIT

# One request, the "result" member as JSON.
ipc () {
	local method="${1}"
	local params="${2:-{\}}"

	printf '{"id":1,"method":"%s","params":%s}\n' "${method}" "${params}" \
		| socat -t 2 - UNIX-CONNECT:"${socket}" \
		| yq -p json -o json -I0 '.result'
}

check () {
	local name="${1}"
	local actual="${2}"
	local expected="${3}"

	if [[ "${actual}" == "${expected}" ]]
	then
		echo "ok - ${name}"
	else
		echo "not ok - ${name}: got '${actual}', expected '${expected}'"
		failures=$((failures + 1))
	fi
}

# Window coordinates of the centre of a [x, y, w, h] rect.
centre () {
	local rect="${1}"

	yq -p json -o tsv '.' <<<"${rect}" | awk '{ printf "%d %d\n", $1 + $3 / 2, $2 + $4 / 2 }'
}

Xvfb "${display_num}" -screen 0 1920x1080x24 >"${work}/xvfb.log" 2>&1 &
xvfb_pid=$!
sleep 1

DISPLAY="${display_num}" XDG_RUNTIME_DIR="${work}" "${editor}" \
	--settings "${work}/editor.yaml" --socket "${socket}" \
	--new-project "${work}/game" --name Smoke >"${work}/editor.log" 2>&1 &
editor_pid=$!

for _ in $(seq 1 50)
do
	[[ -S "${socket}" ]] && break
	sleep 0.2
done
if [[ ! -S "${socket}" ]]
then
	echo "editor-x11-smoke: the editor did not start" >&2
	cat "${work}/editor.log" >&2
	exit 1
fi
sleep 1.5

export DISPLAY="${display_num}"
window="$(xdotool search --name 'libregnum editor' | head -n 1)"
state="$(ipc ui.state)"

# 1. A fast click (press and release between two frames) opens a menu.
read -r fx fy < <(centre "$(yq -p json -o json -I0 '.menubar.File' <<<"${state}")")
xdotool mousemove --window "${window}" "${fx}" "${fy}" click 1
sleep 0.4
check 'fast click opens the File menu' "$(ipc ui.state | yq -p json '.menu')" 'File'

# 2. Escape closes it.
xdotool key Escape
sleep 0.3
check 'Escape closes the menu' "$(ipc ui.state | yq -p json '.menu')" 'null'

# 3. A menu item click runs its action (Play > Play in Editor).
read -r px py < <(centre "$(yq -p json -o json -I0 '.menubar.Play' <<<"${state}")")
frame_h="$(yq -p json '.frame_height' <<<"${state}" | awk '{ printf "%d", $1 }')"
xdotool mousemove --window "${window}" "${px}" "${py}" click 1
sleep 0.4
xdotool mousemove --window "${window}" "$((px + 10))" "$((py + frame_h + 12))" click 1
sleep 0.4
check 'menu item runs its action' "$(ipc play.state | yq -p json '.playing')" 'true'
ipc play.stop >/dev/null

# 4. Keyboard shortcut with a modifier (Ctrl+A selects all).
xdotool mousemove --window "${window}" "${fx}" "$((fy + 300))"
xdotool key ctrl+a
sleep 0.3
check 'Ctrl+A selects every node' "$(ipc selection.get | yq -p json '.nodes | length')" '3'
ipc selection.clear >/dev/null

# 5. Right drag in the viewport rotates the camera.
read -r vx vy < <(centre "$(yq -p json -o json -I0 '.viewport' <<<"${state}")")
before="$(ipc view.get | yq -p json -o json -I0 '.target')"
xdotool mousemove --window "${window}" "${vx}" "${vy}"
xdotool mousedown 3
sleep 0.1
for _ in $(seq 1 10)
do
	xdotool mousemove_relative -- 8 3
	sleep 0.03
done
xdotool mouseup 3
sleep 0.3
after="$(ipc view.get | yq -p json -o json -I0 '.target')"
check 'right drag rotates the camera' "$([[ "${before}" != "${after}" ]] && echo changed || echo same)" 'changed'

# 6. Dragging the orientation gizmo orbits; clicking an end aligns.
read -r gx gy < <(yq -p json -o tsv '.viewport' <<<"${state}" | awk '{ printf "%d %d\n", $1 + 46, $2 + $4 - 46 }')
before="$(ipc view.get | yq -p json -o json -I0 '.position')"
xdotool mousemove --window "${window}" "$((gx + 3))" "$((gy + 3))"
xdotool mousedown 1
sleep 0.1
for _ in $(seq 1 8)
do
	xdotool mousemove_relative -- 6 0
	sleep 0.03
done
xdotool mouseup 1
sleep 0.3
after="$(ipc view.get | yq -p json -o json -I0 '.position')"
check 'dragging the axis gizmo orbits' "$([[ "${before}" != "${after}" ]] && echo changed || echo same)" 'changed'

# 7. Numpad 7 looks from the top (orthographic).
xdotool mousemove --window "${window}" "${vx}" "${vy}"
xdotool key KP_7
sleep 0.3
check 'Num7 gives the top view' "$(ipc view.get | yq -p json '.orthographic')" 'true'

# 8. Wheel zooms.
before="$(ipc view.get | yq -p json -o json -I0 '.')"
xdotool click 4 click 4
sleep 0.3
after="$(ipc view.get | yq -p json -o json -I0 '.')"
check 'wheel zooms' "$([[ "${before}" != "${after}" ]] && echo changed || echo same)" 'changed'

# 9. Alt + F opens the File menu from the keyboard.
xdotool key alt+f
sleep 0.4
check 'Alt+F opens the File menu' "$(ipc ui.state | yq -p json '.menu')" 'File'
xdotool key Escape

ipc editor.quit '{"force":true}' >/dev/null || true
wait "${editor_pid}" 2>/dev/null || true
editor_pid=''

if [[ "${failures}" -gt 0 ]]
then
	echo "editor-x11-smoke: ${failures} check(s) failed"
	exit 1
fi
echo 'editor-x11-smoke: all checks passed'
