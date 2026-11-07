#!/bin/bash
# xrun.sh - run a command on an isolated Xvfb display (never your session)
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
# Every GPU test, editor run and screenshot made by a script or an agent
# goes through this wrapper:
#   - a private Xvfb on a free display number (xvfb-run -a), so nothing ever
#     opens on the developer's Wayland/X session;
#   - a 1920x1200x24 screen, so 1920x1080 windows fit with room to spare
#     (screenshots for docs are taken at 1920x1080);
#   - -noreset: without it Xvfb resets when its last client disconnects and
#     a second window opened by the same process fails with "X11: Failed to
#     open display";
#   - WAYLAND_DISPLAY unset and XDG_SESSION_TYPE=x11, so GLFW picks X11;
#   - an empty, short XDG_RUNTIME_DIR, so GLFW and the editor's IPC socket
#     never reach the session's runtime dir (socket paths must fit 107 bytes);
#   - throwaway XDG_CONFIG_HOME / XDG_DATA_HOME / XDG_CACHE_HOME, so an
#     editor run never reads or rewrites your ~/.config/libregnum/editor.yaml
#     (saved layouts, recent projects) or your caches. Pass -k to keep the
#     real ones (e.g. to reproduce a bug with your own settings).

set -euo pipefail

usage () {
	echo 'usage: build-aux/xrun.sh [-t SECONDS] [-s WxH] COMMAND [ARGS...]'
	echo
	echo '  -t SECONDS   kill the command after SECONDS (default: no limit)'
	echo '  -s WxH       screen size (default: 1920x1200)'
	echo '  -k           keep the real XDG config/data/cache dirs (default: throwaway)'
	echo
	echo 'examples:'
	echo '  build-aux/xrun.sh make test'
	echo '  build-aux/xrun.sh -t 300 tests/build/release/tests/test-pbr'
	echo '  build-aux/xrun.sh build/release/lrgeditor --width 1920 --height 1080 ~/games/demo'
	echo '  build-aux/xrun.sh -k build/release/lrgeditor ~/games/demo    # with your own settings'
}

main () {
	local timeout_s=''
	local size='1920x1200'
	local runtime_dir
	local keep_xdg=0
	local -a xdg_env

	while [[ $# -gt 0 ]]
	do
		case "${1}" in
			-h|--help)
				usage
				exit 0
				;;
			--license)
				echo 'AGPL-3.0-or-later'
				exit 0
				;;
			-t)
				timeout_s="${2:?-t needs seconds}"
				shift 2
				;;
			-s)
				size="${2:?-s needs WxH}"
				shift 2
				;;
			-k)
				keep_xdg=1
				shift
				;;
			--)
				shift
				break
				;;
			-*)
				usage >&2
				exit 2
				;;
			*)
				break
				;;
		esac
	done

	if [[ $# -eq 0 ]]
	then
		usage >&2
		exit 2
	fi

	if ! command -v xvfb-run >/dev/null 2>&1
	then
		echo 'xrun.sh: xvfb-run not found (Fedora: xorg-x11-server-Xvfb)' >&2
		exit 1
	fi

	runtime_dir="$(mktemp -d /tmp/lrgx.XXXXXX)"
	# shellcheck disable=SC2064
	trap "rm -rf -- '${runtime_dir}'" EXIT
	xdg_env=(XDG_RUNTIME_DIR="${runtime_dir}/run")
	mkdir -m 700 "${runtime_dir}/run"
	if [[ ${keep_xdg} -eq 0 ]]
	then
		mkdir "${runtime_dir}/config" "${runtime_dir}/data" "${runtime_dir}/cache"
		xdg_env+=(XDG_CONFIG_HOME="${runtime_dir}/config" XDG_DATA_HOME="${runtime_dir}/data"
		          XDG_CACHE_HOME="${runtime_dir}/cache")
	fi

	if [[ -n "${timeout_s}" ]]
	then
		set -- timeout "${timeout_s}" "$@"
	fi

	xvfb-run -a -s "-screen 0 ${size}x24 -noreset" \
		env -u WAYLAND_DISPLAY XDG_SESSION_TYPE=x11 "${xdg_env[@]}" "$@"
}

main "$@"
