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
#   - without xvfb-run, a private X display from Xwayland on a headless
#     wlroots compositor (gowl, or weston's headless backend) instead,
#     the whole stack in its own D-Bus session (dbus-run-session), so the
#     compositor never registers anything on the user's session bus;
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
	echo 'Uses xvfb-run when it is installed; otherwise Xwayland on a headless'
	echo 'gowl or weston in a private D-Bus session (-s is ignored there; the'
	echo 'headless output decides the size).'
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

	if command -v xvfb-run >/dev/null 2>&1
	then
		xvfb-run -a -s "-screen 0 ${size}x24 -noreset" \
			env -u WAYLAND_DISPLAY XDG_SESSION_TYPE=x11 "${xdg_env[@]}" "$@"
		return
	fi

	if command -v Xwayland >/dev/null 2>&1 \
		&& command -v dbus-run-session >/dev/null 2>&1 \
		&& { command -v gowl >/dev/null 2>&1 || command -v weston >/dev/null 2>&1; }
	then
		# Re-enter this script in a private D-Bus session to start the stack.
		# The bus and anything it activates see only the throwaway runtime
		# dir, and GIO stays local (no gvfs daemon, no FUSE mount).
		env XDG_RUNTIME_DIR="${runtime_dir}/run" GIO_USE_VFS=local GVFS_DISABLE_FUSE=1 \
			dbus-run-session -- "${BASH_SOURCE[0]}" --headless-stack "${runtime_dir}" \
			env "${xdg_env[@]}" "$@"
		return
	fi

	echo 'xrun.sh: no isolated X display available: install xvfb-run (Fedora: xorg-x11-server-Xvfb),' >&2
	echo '         or Xwayland, dbus-run-session and a headless wlroots compositor (gowl or weston)' >&2
	exit 1
}

# headless_stack <runtime-dir> COMMAND...: start a headless compositor and
# Xwayland on a free display number, all sockets under <runtime-dir>, run
# COMMAND with DISPLAY set and WAYLAND_DISPLAY unset, then stop both.  Runs
# inside dbus-run-session (see main), so DBUS_SESSION_BUS_ADDRESS is private.
headless_stack () {
	if [[ $# -lt 2 ]]
	then
		echo "headless_stack() requires at least 2 positional arguments"
		echo 'headless_stack <runtime-dir> COMMAND...'
		exit 1
	fi

	local base="${1}"
	local comp_run="${base}/comp-run"
	local display_num=''
	local sock=''
	local comp_pid
	local x_pid
	local rc=0
	local n
	local _

	shift
	mkdir -m 700 "${comp_run}" "${base}/comp-config" "${base}/comp-data" "${base}/comp-cache"

	# A display number nobody uses (socket and lock file both free).
	for n in $(seq 90 199)
	do
		if [[ ! -e "/tmp/.X11-unix/X${n}" && ! -e "/tmp/.X${n}-lock" ]]
		then
			display_num="${n}"
			break
		fi
	done
	if [[ -z "${display_num}" ]]
	then
		echo 'xrun.sh: no free X display number in 90..199' >&2
		exit 1
	fi

	# The compositor: its own runtime/config dirs, no input devices, the
	# software renderer (it only has to exist; clients render with Mesa).
	if command -v gowl >/dev/null 2>&1
	then
		env -u WAYLAND_DISPLAY -u DISPLAY \
			XDG_RUNTIME_DIR="${comp_run}" XDG_CONFIG_HOME="${base}/comp-config" \
			XDG_DATA_HOME="${base}/comp-data" XDG_CACHE_HOME="${base}/comp-cache" \
			WLR_BACKENDS=headless WLR_LIBINPUT_NO_DEVICES=1 WLR_RENDERER=pixman \
			gowl > "${base}/compositor.log" 2>&1 &
	else
		env -u WAYLAND_DISPLAY -u DISPLAY \
			XDG_RUNTIME_DIR="${comp_run}" XDG_CONFIG_HOME="${base}/comp-config" \
			weston --backend=headless --socket=wayland-0 --idle-time=0 \
			> "${base}/compositor.log" 2>&1 &
	fi
	comp_pid=$!

	for _ in $(seq 100)
	do
		sock="$(find "${comp_run}" -maxdepth 1 -name 'wayland-*' -type s -print -quit)"
		[[ -n "${sock}" ]] && break
		sleep 0.1
	done
	if [[ -z "${sock}" ]]
	then
		echo "xrun.sh: the headless compositor did not start (see ${base}/compositor.log)" >&2
		kill "${comp_pid}" 2>/dev/null || true
		exit 1
	fi

	XDG_RUNTIME_DIR="${comp_run}" WAYLAND_DISPLAY="${sock##*/}" \
		Xwayland ":${display_num}" -noreset > "${base}/xwayland.log" 2>&1 &
	x_pid=$!
	for _ in $(seq 100)
	do
		[[ -S "/tmp/.X11-unix/X${display_num}" ]] && break
		sleep 0.1
	done

	env -u WAYLAND_DISPLAY DISPLAY=":${display_num}" XDG_SESSION_TYPE=x11 "$@" || rc=$?

	kill "${x_pid}" "${comp_pid}" 2>/dev/null || true
	wait "${x_pid}" "${comp_pid}" 2>/dev/null || true
	return "${rc}"
}

if [[ "${1:-}" == '--headless-stack' ]]
then
	shift
	headless_stack "$@"
	exit $?
fi

main "$@"
