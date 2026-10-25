#!/bin/bash
# libregnum - lrgeditorctl smoke test
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
# Starts a headless lrgeditor on a fresh project and drives it with
# lrgeditorctl: sessions, get/describe, create/set/rename/move/delete,
# apply -f, edit (scripted $VISUAL), select, run, undo/redo, save,
# validate, logs, call, events, and the MCP bridge (lrgeditorctl mcp)
# and in-process MCP (lrgeditor --mcp) when built in.

set -euo pipefail

usage () {
	echo 'editorctl-smoke.sh - exercise lrgeditorctl against a headless lrgeditor'
	echo ''
	echo 'Usage: editorctl-smoke.sh [-h|--help] [--license] [BUILD_DIR]'
	echo ''
	echo 'Examples:'
	echo '  tests/editorctl-smoke.sh build/release'
	echo '  make test-editorctl'
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

build="${1:-build/release}"
editor="${build}/lrgeditor"
ctl="${build}/lrgeditorctl"
for bin in "${editor}" "${ctl}"
do
	if [[ ! -x "${bin}" ]]
	then
		echo "editorctl-smoke: ${bin} is not built" >&2
		exit 1
	fi
done

# Short private runtime dir: sockets must fit sun_path, and the test must
# not see (or be seen by) editors the user is running.
work="$(mktemp -d "${XDG_RUNTIME_DIR:-/tmp}/lrg-ctl.XXXXXX")"
chmod 700 "${work}"
export XDG_RUNTIME_DIR="${work}"
unset LRG_EDITOR_SOCKET DISPLAY WAYLAND_DISPLAY
editor_pid=''
failures=0

cleanup () {
	[[ -n "${editor_pid}" ]] && kill "${editor_pid}" 2>/dev/null || true
	rm -rf -- "${work}"
}
trap cleanup EXIT

check () {
	local name="${1}"
	local actual="${2}"
	local expected="${3}"

	if [[ "${actual}" == *"${expected}"* ]]
	then
		echo "ok - ${name}"
	else
		echo "not ok - ${name}"
		echo "#   expected to contain: ${expected}"
		echo "#   got: ${actual}"
		failures=$((failures + 1))
	fi
}

ctl () {
	"${ctl}" "${@}" 2>&1 || true
}

"${editor}" --settings "${work}/editor.yaml" --new-project "${work}/game" --name Smoke --validate >/dev/null
"${editor}" --settings "${work}/editor.yaml" --headless "${work}/game" >"${work}/editor.log" 2>&1 &
editor_pid=$!
for _ in $(seq 1 50)
do
	compgen -G "${work}/libregnum/editor-*.sock" >/dev/null && break
	sleep 0.1
done

# Sessions and reads.
check 'sessions lists the editor' "$(ctl sessions)" "${work}/game"
check 'get nodes (table)' "$(ctl get nodes)" 'Ground   primitive'
check 'get nodes -o name' "$(ctl get nodes -o name)" 'node/Camera'
check 'get info -o json' "$(ctl get info -o json)" '"nodes" : 3'
check 'session by project substring' "$(ctl -s game get project)" 'name: Smoke'

# Writes.
check 'create node' "$(ctl create node Props)" 'node/Props created'
check 'create child with transform' \
	"$(ctl create node Crate --parent Props --primitive cube location=2,0.5,-3)" 'node/Props/Crate created'
check 'set node' "$(ctl set node Props/Crate rotation_degrees=0,45,0 scale=2,2,2)" 'configured'
check 'get node yaml' "$(ctl get node Props/Crate)" 'primitive: cube'
check 'rename node' "$(ctl rename node Props/Crate Box)" 'node/Props/Box renamed'
check 'move to top level' "$(ctl move node Props/Box)" 'node/Box moved'

printf 'node: Props/Barrel\nprimitive: cylinder\nlocation: [3, 0.5, 0]\n---\nnode: Box\nvisible: false\n' >"${work}/apply.yaml"
check 'apply creates and updates' "$(ctl apply -f "${work}/apply.yaml")" 'node/Props/Barrel created'
check 'apply creates missing parents' \
	"$(printf 'node: Keep/Hall/Table\nprimitive: cube\n' | "${ctl}" apply -f - 2>&1)" 'node/Keep/Hall created'
check 'apply from stdin (JSON)' "$(echo '{"node":"Lamp","kind":"light"}' | "${ctl}" apply -f - 2>&1)" 'node/Lamp created'
# shellcheck disable=SC2016  # the test name shows $VISUAL literally
check 'edit via $VISUAL' "$(VISUAL='sed -i s/name:\ Lamp/name:\ Sunlamp/' ctl edit node Lamp)" 'node/Sunlamp edited'
check 'edit without change' "$(VISUAL=true ctl edit node Sunlamp)" 'no changes made'

check 'select' "$(ctl select Box Sunlamp && ctl get selection)" 'Sunlamp'
ctl run edit.deselect >/dev/null
check 'run action' "$(ctl get selection -o json)" '[]'
check 'history' "$(ctl get history)" 'Add Props'
check 'undo' "$(ctl undo)" 'undone'
check 'redo' "$(ctl redo)" 'redone'
check 'delete' "$(ctl delete node Sunlamp)" 'node/Sunlamp deleted'
check 'call raw method' "$(ctl call node.find query=Barr -o name)" 'Barrel'
check 'notify' "$(ctl notify hello from the smoke test -o json)" 'true'
check 'view set/get' "$(ctl view set target=1,2,3 && ctl get view)" '- 1'

# Persistence and validation.
check 'save' "$(ctl save)" 'saved'
check 'saved file has the nodes' "$(cat "${work}/game/levels/main.rlevel")" 'Barrel'
check 'validate passes' "$(ctl validate; echo "status=$?")" 'status=0'

# Errors and exit codes.
"${ctl}" get node nope >/dev/null 2>&1 && status=0 || status=$?
check 'missing node exits 1' "status=${status}" 'status=1'
"${ctl}" frobnicate >/dev/null 2>&1 && status=0 || status=$?
check 'usage error exits 2' "status=${status}" 'status=2'

# Logs and events.
check 'logs' "$(ctl logs --tail 50)" 'editor'
( "${ctl}" events changed >"${work}/events.out" 2>&1 & echo $! >"${work}/events.pid" )
sleep 0.4
ctl set node Box visible=true >/dev/null
sleep 0.4
kill "$(cat "${work}/events.pid")" 2>/dev/null || true
check 'events stream' "$(cat "${work}/events.out")" '"event":"changed"'

# MCP bridge (only when built with MCP).
if "${ctl}" mcp </dev/null 2>&1 | grep -q 'without MCP'
then
	echo 'ok - mcp skipped (built without MCP)'
else
	mcp_in='{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2025-06-18","capabilities":{},"clientInfo":{"name":"smoke","version":"1"}}}
{"jsonrpc":"2.0","method":"notifications/initialized"}
{"jsonrpc":"2.0","id":2,"method":"tools/list"}
{"jsonrpc":"2.0","id":3,"method":"tools/call","params":{"name":"create_node","arguments":{"name":"AIBox","primitive":"torus","location":[0,1,0]}}}
{"jsonrpc":"2.0","id":4,"method":"tools/call","params":{"name":"get_node","arguments":{"node":"nope"}}}'
	# Keep stdin open briefly, as real MCP clients do, so every reply is
	# written before the session ends.
	out="$({ printf '%s\n' "${mcp_in}"; sleep 2; } | timeout 20 "${ctl}" mcp 2>&1)"
	check 'mcp initialize' "${out}" '"name":"libregnum-editor"'
	check 'mcp tools/list' "${out}" '"name":"create_node"'
	check 'mcp create_node' "$(ctl get nodes -o name)" 'node/AIBox'
	check 'mcp tool error' "${out}" '"isError":true'
fi

ctl quit --force >/dev/null
wait "${editor_pid}" 2>/dev/null || true
editor_pid=''

# In-process MCP: lrgeditor --mcp, stdout must stay pure JSON-RPC.
if "${editor}" --help 2>/dev/null | grep -q -- '--mcp'
then
	out="$({ printf '%s\n' \
		'{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2025-06-18","capabilities":{},"clientInfo":{"name":"smoke","version":"1"}}}' \
		'{"jsonrpc":"2.0","method":"notifications/initialized"}' \
		'{"jsonrpc":"2.0","id":2,"method":"tools/call","params":{"name":"editor_info","arguments":{}}}'; sleep 2; } \
		| timeout 30 "${editor}" --mcp --settings "${work}/editor.yaml" "${work}/game" 2>/dev/null)"
	check 'lrgeditor --mcp answers' "${out}" 'game/levels/main.rlevel'
	check 'lrgeditor --mcp stdout is only JSON' "$(grep -cv '^{' <<<"${out}" || true)" '0'
fi

if [[ "${failures}" -gt 0 ]]
then
	echo "editorctl-smoke: ${failures} check(s) failed"
	exit 1
fi
echo 'editorctl-smoke: all checks passed'
