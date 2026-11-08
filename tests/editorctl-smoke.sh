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
# validate, logs, call, events, the 0.8.1 verbs (meta, vcs, prefab, code,
# data, palette, preferences, hierarchy filter, notify --level), the
# 0.9.0 rendering verbs (material, shader, shadergraph, env, post, lod,
# view layers; skipped until the editor registers their methods), the
# 0.9.0 M3/M4 verbs (bake, seq, skel2d, anim, bonemap; likewise skipped),
# the 0.9.0 M5 physics verbs (physasset, physics layers, physics),
# the 0.9.0 M6/M7 verbs (asset new, nav, bt, fsm, ai, particles, weather,
# terrain, water, mixer, audio, tileset, sprite, tiles; likewise skipped),
# the 0.9.0 M8 verbs (debug, lsp, profiler, frame, script, vscript,
# memory, network; likewise skipped)
# and the MCP bridge (lrgeditorctl mcp)
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

# Editor tools: tiles, timelines, thumbnails.
ctl create node Floor --kind tilemap >/dev/null
check 'tiles configure' "$(ctl tiles Floor configure width=4 height=2 -o json)" '"width" : 4'
check 'tiles paint stroke' "$(ctl tiles Floor paint tile=2 'points=[[0,0],[3,0]]')" 'changed: 4'
check 'tiles fill' "$(ctl tiles Floor fill x=0 y=1 tile=5)" 'changed: 4'
check 'tiles grid' "$(ctl tiles Floor)" '    2   2   2   2'
ctl create node Floor2 kind=tilemap >/dev/null
check 'create node kind=tilemap makes a tilemap' "$(ctl tiles Floor2 configure width=2 height=2 -o json)" '"width" : 2'
ctl delete node Floor2 >/dev/null
check 'timeline key' "$(ctl timeline key Spin Box time=1 channel=rotation.y value=3.14 easing=ease-in-out-sine)" 'keys: 1'
check 'timeline list' "$(ctl timeline)" 'name: Spin'
check 'timeline get' "$(ctl timeline get Spin)" 'ease-in-out-sine'
printf '\x89PNG\r\n' >"${work}/game/broken.png"
check 'thumbnail of a level' "$(ctl thumbnail "${work}/game/levels/main.rlevel")" '.cache/thumbnails/'
"${ctl}" thumbnail "${work}/game/broken.png" >/dev/null 2>&1 && status=0 || status=$?
check 'thumbnail failure exits 1' "status=${status}" 'status=1'

# 0.8.1 verbs: asset sidecars, git status, linked prefabs, code editor.
mkdir -p "${work}/game/assets" "${work}/game/prefabs" "${work}/game/scripts"
printf 'stone' >"${work}/game/assets/stone.png"
check 'meta get (in memory)' "$(ctl meta get "${work}/game/assets/stone.png")" 'exists: false'
check 'meta set' "$(ctl meta set "${work}/game/assets/stone.png" filter=point)" 'filter: point'
check 'meta sidecar written' "$(cat "${work}/game/assets/stone.png.meta")" 'point'
"${ctl}" meta set "${work}/game/assets/stone.png" filter=fuzzy >/dev/null 2>&1 && status=0 || status=$?
check 'meta set rejects a bad value' "status=${status}" 'status=1'
check 'vcs outside git' "$(ctl vcs)" 'not a git repository'
check 'vcs path outside git' "$(ctl vcs "${work}/game/assets/stone.png" -o json)" '"path"'
ctl call prefab.save node=Props/Barrel "path=${work}/game/prefabs/barrel.rprefab" >/dev/null
ctl call prefab.instantiate "path=${work}/game/prefabs/barrel.rprefab" parent=Keep >/dev/null
check 'prefab overrides' "$(ctl prefab overrides Keep/Barrel)" 'linked: true'
ctl set node Keep/Barrel scale=3,3,3 >/dev/null
check 'prefab override recorded' "$(ctl prefab overrides Keep/Barrel -o json)" 'transform'
check 'prefab revert' "$(ctl prefab revert Keep/Barrel -o json)" '"overrides" : []'
check 'prefab sync' "$(ctl prefab sync)" 'synced'
check 'prefab unlink' "$(ctl prefab unlink Keep/Barrel)" 'linked: false'
check 'prefab relink' "$(ctl prefab relink Keep/Barrel "path=${work}/game/prefabs/barrel.rprefab")" 'linked: true'
printf 'print("smoke")\nreturn 1\n' >"${work}/game/scripts/smoke.lua"
check 'code open' "$(ctl code open "${work}/game/scripts/smoke.lua:2")" 'smoke.lua:2'
check 'code list' "$(ctl code list)" 'smoke.lua'
check 'code save' "$(ctl code save -o json)" '"dirty" : false'
check 'methods include code.open' "$(ctl get methods -o json)" 'code.open'

# 0.8.1 data editors. lrgeditor --headless installs the data.* methods
# and runs the full document model (open, get/set/edit, undo, validate,
# save, close); only drawing the panels needs a window.
mkdir -p "${work}/game/data"
printf 'actions:\n  jump:\n    bindings:\n      - { type: keyboard, key: SPACE }\n' >"${work}/game/data/input.yaml"
check 'data open' "$(ctl data open "${work}/game/data/input.yaml")" 'format: input-map'
check 'data list' "$(ctl data list)" 'input-map'
check 'data get (focused editor)' "$(ctl data get /actions/jump/bindings/0/key)" 'value: SPACE'
check 'data set' "$(ctl data set /actions/jump/bindings/0/key ENTER)" 'dirty: true'
check 'data get by file' "$(ctl data get "${work}/game/data/input.yaml" /actions/jump/bindings/0/key -o json)" '"value" : "ENTER"'
check 'data edit add_key' "$(ctl data edit add_key /actions key=fire 'yaml={bindings: []}')" 'Add /actions/fire'
check 'data edit rename_key' "$(ctl data edit rename_key /actions old=fire new=shoot)" 'Rename /actions/shoot'
check 'data undo' "$(ctl data undo -o json)" '"ok" : true'
check 'data redo' "$(ctl data redo -o json)" '"ok" : true'
check 'data validate' "$(ctl data validate -o json)" '"errors" : 0'
check 'data reveal' "$(ctl data reveal /actions/jump)" 'format: input-map'
check 'data save' "$(ctl data save -o json)" '"dirty" : false'
check 'data saved file' "$(cat "${work}/game/data/input.yaml")" 'key: ENTER'
check 'data reload' "$(ctl data reload -o json)" '"ok" : true'
check 'data close' "$(ctl data close)" 'closed: true'
check 'data list empty' "$(ctl data list -o json)" '[]'
"${ctl}" data frobnicate >/dev/null 2>&1 && status=0 || status=$?
check 'data usage error exits 2' "status=${status}" 'status=2'

# notify --level takes a name or 0-3 (an error toast logs a warning).
ctl notify smoke error toast --level error >/dev/null
check 'notify --level error' "$(ctl logs --tail 1)" 'warning'
ctl notify smoke numeric toast --level 3 >/dev/null
check 'notify --level 3' "$(ctl logs --tail 1)" 'warning'
"${ctl}" notify nope --level loud >/dev/null 2>&1 && status=0 || status=$?
check 'notify bad level exits 2' "status=${status}" 'status=2'

# Hierarchy filter, palette and preferences (the dialogs open headless
# too; their state is in the view model).
check 'hierarchy filter' "matches=$(ctl hierarchy filter Box -o json);" 'matches=1;'
check 'hierarchy filter in state' "$(ctl hierarchy state -o json)" '"filter" : "Box"'
ctl hierarchy filter >/dev/null
check 'hierarchy filter clear' "$(ctl hierarchy state -o json)" '"filter" : null'
check 'palette' "$(ctl palette commands undo -o json)" '"open" : true'
check 'preferences' "$(ctl preferences keybindings)" 'preferences/keybindings opened'
"${ctl}" preferences nope >/dev/null 2>&1 && status=0 || status=$?
check 'preferences unknown page exits 1' "status=${status}" 'status=1'

# 0.9.0 rendering verbs: materials, shaders, shader graphs (ED1) and the
# level look, post stacks, LOD and render layers (ED2). A family whose
# methods this editor build does not register yet is skipped.
methods="$(ctl get methods -o json)"
has_method () {
	[[ "${methods}" == *"\"${1}\""* ]]
}
skip () {
	echo "ok - SKIP ${1} (methods not registered by this editor build)"
}
mkdir -p "${work}/game/materials" "${work}/game/shaders" "${work}/game/graphs" "${work}/game/looks"
"${ctl}" material frobnicate >/dev/null 2>&1 && status=0 || status=$?
check 'material usage error exits 2' "status=${status}" 'status=2'
"${ctl}" lod >/dev/null 2>&1 && status=0 || status=$?
check 'lod usage error exits 2' "status=${status}" 'status=2'
"${ctl}" view layers bogus >/dev/null 2>&1 && status=0 || status=$?
check 'view layers bad mask exits 2' "status=${status}" 'status=2'

if has_method material.create
then
	mat="${work}/game/materials/smoke.rmat"
	check 'material create' "$(ctl material create "${mat}")" 'smoke.rmat'
	check 'material set' "$(ctl material set "${mat}" metallic 1 -o json)" '"key" : "metallic"'
	check 'material get' "$(ctl material get "${mat}" -o json)" '"metallic" : 1'
	check 'material list' "$(ctl material list)" 'editors'
	check 'material save' "$(ctl material save "${mat}")" 'smoke.rmat'
	# material editors are data editors: close it so the MCP data_list
	# check below sees no open editors
	ctl data close "${mat}" --force >/dev/null
else
	skip 'material'
fi
if has_method shader.create
then
	check 'shader create' "$(ctl shader create "${work}/game/shaders/smoke.rshader")" 'smoke.rshader'
	check 'shader uniforms' "$(ctl shader uniforms "${work}/game/shaders/smoke.rshader")" 'uniforms'
else
	skip 'shader'
fi
if has_method shadergraph.create
then
	graph="${work}/game/graphs/smoke.rshadergraph"
	check 'shadergraph create' "$(ctl shadergraph create "${graph}")" 'smoke.rshadergraph'
	check 'shadergraph types' "$(ctl shadergraph types)" 'category'
	check 'shadergraph add' "$(ctl shadergraph add "${graph}" time -o json)" '"id"'
	check 'shadergraph validate' "$(ctl shadergraph validate "${graph}" -o json)" '"ok"'
	ctl data close "${graph}" --force >/dev/null
else
	skip 'shadergraph'
fi
if has_method environment.get
then
	check 'env set' "$(ctl env set fog.density 0.05 -o json)" '0.05'
	check 'env get' "$(ctl env -o json)" '"present" : true'
	check 'env set sky colour' "$(ctl env set sky.zenith 0.1,0.2,0.6 -o json)" '0.6'
	check 'env progress' "$(ctl env progress -o json)" '"baking"'
	check 'env reset' "$(ctl env reset -o json)" '"present" : false'
	check 'env reset undo' "$(ctl undo && ctl env -o json)" '"present" : true'
else
	skip 'environment'
fi
if has_method post.create
then
	look="${work}/game/looks/smoke.rpost"
	check 'post types' "$(ctl post types)" 'vignette'
	check 'post create' "$(ctl post create "${look}" preset=empty)" 'smoke.rpost'
	check 'post add' "$(ctl post add "${look}" vignette -o json)" '"index" : 0'
	check 'post set' "$(ctl post set "${look}" 0 enabled false -o json)" 'true'
	check 'post get' "$(ctl post get "${look}" -o json)" '"type" : "vignette"'
	check 'post save' "$(ctl post save "${look}")" 'smoke.rpost'
	check 'post file written' "$(cat "${look}")" 'vignette'
	if has_method environment.get
	then
		check 'post assign' "$(ctl post assign "${look}")" 'smoke.rpost'
	fi
	# the post stack editor is a data editor: close it so the MCP
	# data_list check below sees no open editors
	ctl data close "${look}" --force >/dev/null
else
	skip 'post'
fi
if has_method lod.get
then
	"${ctl}" lod get Ground >/dev/null 2>&1 && status=0 || status=$?
	# a primitive has no LOD levels: an editor error (1) or an empty
	# answer (0) are both fine, a usage error (2) is not
	check 'lod get reaches the editor' "$([[ "${status}" -ne 2 ]] && echo sent || echo usage)" 'sent'
else
	skip 'lod'
fi
if has_method view.layers
then
	check 'view layers mask' "$(ctl view layers 0x5 -o json)" '"mask" : 5'
	check 'view layers one layer off' "$(ctl view layers 0 off -o json)" '"mask" : 4'
	check 'view layers all' "$(ctl view layers all -o json)" '"mask" : 4294967295'
	check 'view mode overdraw' "$(ctl view mode overdraw)" 'mode: overdraw'
	ctl view mode lit >/dev/null
else
	skip 'view layers'
fi

# 0.9.0 M3/M4 editor wave: the lighting bake (EDBAKE), sequences (EDSEQ),
# 2D skeletons (EDRIG) and animation graphs / bone maps / clips (EDANIM).
"${ctl}" bake frobnicate >/dev/null 2>&1 && status=0 || status=$?
check 'bake usage error exits 2' "status=${status}" 'status=2'
"${ctl}" bake evaluate 1 2 >/dev/null 2>&1 && status=0 || status=$?
check 'bake evaluate needs three numbers' "status=${status}" 'status=2'
if has_method bake.get
then
	check 'bake set' "$(ctl bake set samples 8 -o json)" '"samples"'
	check 'bake set writes the level' "$(ctl bake -o json)" '"set_keys" : ['
	check 'bake set texels' "$(ctl bake set texels_per_unit 2 -o json)" '"texels_per_unit" : 2'
	# nothing static yet: the bake refuses, exit 1
	"${ctl}" bake start >/dev/null 2>&1 && status=0 || status=$?
	check 'bake without static geometry fails' "status=${status}" 'status=1'
	ctl call node.create name=BakeFloor kind=primitive primitive=cube >/dev/null
	ctl call node.set node=BakeFloor 'visual={"params":{"static_lighting":true}}' >/dev/null 2>&1 || \
		ctl call inspector.set node=BakeFloor section=visual name=param-static_lighting value=true >/dev/null 2>&1 || true
	check 'bake start --wait' "$(ctl bake start --wait -o json 2>&1)" '"entries" : 1'
	check 'bake progress' "$(ctl bake progress -o json)" '"phase" : "done"'
	check 'bake saved next to the level' "$(ls "${work}/game/levels")" 'main.rlightmap'
	check 'bake evaluate' "$(ctl bake evaluate 0 0.6 0 0 1 0 -o json)" '"luminance"'
	check 'bake preview off' "$(ctl bake preview off -o json)" '"show" : false'
	ctl bake preview on >/dev/null
	check 'bake clear' "$(ctl bake clear -o json)" '"cleared" : true'
	check 'bake clear undo' "$(ctl undo && ctl bake -o json)" 'main.rlightmap'
	check 'bake reset' "$(ctl bake reset -o json | tr -d ' \n')" '"set_keys":[]'
else
	skip 'bake'
fi
if has_method sequence.create
then
	seqf="${work}/game/smoke.rseq"
	check 'seq create' "$(ctl seq create "${seqf}" length=4)" 'smoke.rseq'
	check 'seq list' "$(ctl seq -o json)" 'smoke.rseq'
	check 'seq add-track' "$(ctl seq add-track event -o json)" '"index" : 0'
	check 'seq add-section' "$(ctl seq add-section 0 1 name=boom -o json)" '"index"'
	check 'seq get' "$(ctl seq get -o json)" '"kind" : "event"'
	check 'seq scrub' "$(ctl seq scrub 2 -o json)" 'boom'
	check 'seq save' "$(ctl seq save)" 'smoke.rseq'
else
	skip 'seq'
fi
if has_method skel2d.create
then
	rig="${work}/game/smoke.rskel2d"
	check 'skel2d create' "$(ctl skel2d create "${rig}")" 'smoke.rskel2d'
	check 'skel2d add-bone' "$(ctl skel2d add-bone "${rig}" arm length=40 -o json)" '"bone" : "arm"'
	check 'skel2d get' "$(ctl skel2d get "${rig}" -o json)" '"arm"'
	check 'skel2d save' "$(ctl skel2d save "${rig}")" 'smoke.rskel2d'
	ctl data close "${rig}" --force >/dev/null 2>&1 || true
else
	skip 'skel2d'
fi
if has_method anim.create
then
	graphf="${work}/game/smoke.ranim"
	check 'anim create' "$(ctl anim create "${graphf}")" 'smoke.ranim'
	check 'anim add-param' "$(ctl anim add-param "${graphf}" speed float -o json)" 'speed'
	check 'anim add-state' "$(ctl anim add-state "${graphf}" idle -o json)" 'idle'
	check 'anim get' "$(ctl anim get "${graphf}" -o json)" '"idle"'
	check 'anim validate' "$(ctl anim validate "${graphf}" -o json)" '"ok"'
	check 'anim save' "$(ctl anim save "${graphf}")" 'smoke.ranim'
	ctl data close "${graphf}" --force >/dev/null 2>&1 || true
else
	skip 'anim'
fi
if has_method bonemap.create
then
	mapf="${work}/game/smoke.rbonemap"
	check 'bonemap create' "$(ctl bonemap create "${mapf}")" 'smoke.rbonemap'
	check 'bonemap set' "$(ctl bonemap set "${mapf}" Hips hips -o json 2>&1)" 'Hips'
	check 'bonemap get' "$(ctl bonemap get "${mapf}" -o json)" '"pairs"'
	ctl data close "${mapf}" --force >/dev/null 2>&1 || true
else
	skip 'bonemap'
fi
# 0.9.0 M5 physics verbs (physasset, physics layers, physics; skipped
# until the editor registers their methods).
if has_method physasset.create
then
	paf="${work}/game/smoke.rphysasset"
	check 'physasset create' "$(ctl physasset create "${paf}")" 'smoke.rphysasset'
	check 'physasset set' "$(ctl physasset set "${paf}" name=Smoke -o json)" '"Smoke"'
	check 'physasset add-part' "$(ctl physasset add-part "${paf}" pelvis shape=sphere radius=0.2 -o json)" '"pelvis"'
	check 'physasset set-part' "$(ctl physasset set-part "${paf}" pelvis mass=12.5 -o json)" '12.5'
	check 'physasset get' "$(ctl physasset get "${paf}" -o json)" '"sphere"'
	check 'physasset save' "$(ctl physasset save "${paf}")" 'smoke.rphysasset'
	check 'physasset file' "$(cat "${paf}")" 'bone: pelvis'
	ctl data close "${paf}" --force >/dev/null 2>&1 || true
else
	skip 'physasset'
fi
if has_method physics.layers.get
then
	check 'physics layers' "$(ctl physics layers -o json)" '"ragdoll"'
	check 'physics layers name' "$(ctl physics layers name 9 water -o json)" '"water"'
	check 'physics layers collide' "$(ctl physics layers collide water debris off -o json | tr -d ' \n')" '["debris","water"]'
	check 'physics layers settings' "$(ctl physics layers settings gravity=0,-3.7,0 iterations=12 -o json)" '"iterations" : 12'
	check 'project.ryaml has the physics block' "$(cat "${work}/game/project.ryaml")" 'water'
else
	skip 'physics layers'
fi
if has_method physics.get
then
	check 'physics add-body' "$(ctl physics add-body dynamic --node Box -o json)" '"body"'
	check 'physics set-body' "$(ctl physics set-body mass=20 --node Box -o json)" '20'
	check 'physics get' "$(ctl physics get --node Box -o json)" '"shapes"'
	check 'physics overlay' "$(ctl physics overlay -o json)" '"flags"'
	ctl undo >/dev/null; ctl undo >/dev/null
else
	skip 'physics'
fi
# 0.9.0 M6/M7 verbs: one pass per area (navigation, AI, effects,
# terrain/water, audio, 2D) plus asset.new; each skipped until the editor
# registers its methods.
if has_method asset.new
then
	check 'asset new bt' "$(ctl asset new bt path=ai/smoke.rbt -o json)" 'smoke.rbt'
	check 'asset new file is a tree' "$(cat "${work}/game/ai/smoke.rbt")" 'type: sequence'
	check 'asset new terrain' "$(ctl asset new terrain path=terrain/smoke.rterrain -o json)" '"kind" : "terrain"'
	check 'asset new mixer (free name)' "$(ctl asset new mixer -o json)" '.rmixer'
	"${ctl}" asset new frobnicate path=x.txt >/dev/null 2>&1 && status=0 || status=$?
	check 'asset new unknown kind exits 1' "status=${status}" 'status=1'
else
	skip 'asset new'
fi
# The Navigation and Terrain panels are docked in the default window, so
# nav.* and terrain.* always exist (no skip).
check 'nav get' "$(ctl nav -o json)" '"bake"'
check 'nav set' "$(ctl nav set agent_radius=0.4 -o json)" '0.4'
check 'nav area-add' "$(ctl nav area-add water cost=4 -o json)" '"index"'
check 'nav area-box-add' "$(ctl nav area-box-add water min=-1,0,-1 max=1,1,1 -o json)" '"index"'
check 'nav link-add' "$(ctl nav link-add start=0,0,0 end=0,0,3 -o json)" '"index"'
check 'nav overlay' "$(ctl nav overlay 'on=["polygons"]' -o json)" 'polygons'
check 'nav bake' "$(ctl nav bake save=false -o json)" '"triangles"'
check 'nav agents (not playing)' "$(ctl nav agents -o json)" '"playing" : false'
if has_method bt.create
then
	btf="${work}/game/ai/guard.rbt"
	check 'bt create' "$(ctl bt create "${btf}")" 'guard.rbt'
	check 'bt palette' "$(ctl bt palette -o json)" '"sequence"'
	check 'bt get' "$(ctl bt get "${btf}" -o json)" '"nodes"'
	check 'bt validate' "$(ctl bt validate "${btf}" -o json)" '"ok"'
	check 'bt save' "$(ctl bt save "${btf}")" 'guard.rbt'
	ctl data close "${btf}" --force >/dev/null 2>&1 || true
else
	skip 'bt'
fi
if has_method fsm.create
then
	fsmf="${work}/game/ai/door.rfsm"
	check 'fsm create' "$(ctl fsm create "${fsmf}")" 'door.rfsm'
	check 'fsm add-state' "$(ctl fsm add-state "${fsmf}" open -o json)" 'open'
	check 'fsm add-transition' "$(ctl fsm add-transition "${fsmf}" idle open event=use -o json)" '"index"'
	check 'fsm get' "$(ctl fsm get "${fsmf}" -o json)" '"use"'
	check 'fsm save' "$(ctl fsm save "${fsmf}")" 'door.rfsm'
	check 'fsm file' "$(cat "${fsmf}")" 'open'
	ctl data close "${fsmf}" --force >/dev/null 2>&1 || true
else
	skip 'fsm'
fi
if has_method ai.entities
then
	check 'ai entities (not playing)' "$(ctl ai entities -o json)" '"playing" : false'
	check 'ai brains' "$(ctl ai brains -o json)" '"brains"'
else
	skip 'ai'
fi
if has_method particles.create
then
	pf="${work}/game/fx/sparks.rparticles"
	check 'particles create' "$(ctl particles create "${pf}" -o json)" 'sparks.rparticles'
	check 'particles kinds' "$(ctl particles kinds -o json)" '"stage"'
	check 'particles add-emitter' "$(ctl particles add-emitter "${pf}" name=embers -o json)" '"embers"'
	check 'particles seek' "$(ctl particles seek "${pf}" 0.5 -o json)" '"time"'
	check 'particles get' "$(ctl particles get "${pf}" -o json)" '"emitters"'
	check 'particles save' "$(ctl particles save "${pf}")" 'sparks.rparticles'
	check 'particles file' "$(cat "${pf}")" 'embers'
	ctl data close "${pf}" --force >/dev/null 2>&1 || true
else
	skip 'particles'
fi
if has_method weather.create
then
	wf="${work}/game/fx/sky.rweather"
	check 'weather create' "$(ctl weather create "${wf}" -o json)" 'sky.rweather'
	check 'weather kinds' "$(ctl weather kinds -o json)" '"kinds"'
	check 'weather add-preset' "$(ctl weather add-preset "${wf}" fog -o json)" '"index"'
	check 'weather get' "$(ctl weather get "${wf}" -o json)" '"fog"'
	check 'weather save' "$(ctl weather save "${wf}")" 'sky.rweather'
	ctl data close "${wf}" --force >/dev/null 2>&1 || true
else
	skip 'weather'
fi
check 'terrain create' "$(ctl terrain create name=island -o json)" 'island'
check 'terrain brush' "$(ctl terrain brush tool=raise radius=4 -o json)" '"raise"'
check 'terrain get' "$(ctl terrain get --node island -o json)" '"chunks"'
check 'terrain stroke' "$(ctl terrain stroke 'points=[[0,0],[2,0]]' local=true --node island -o json)" '"changed"'
check 'terrain sample' "$(ctl terrain sample 1 1 local=true --node island -o json)" '"hit"'
check 'terrain add-layer' "$(ctl terrain add-layer name=sand --node island -o json)" '"index"'
check 'terrain save' "$(ctl terrain save --node island -o json)" 'island.rterrain'
check 'terrain create water' "$(ctl terrain create kind=water name=lake -o json)" 'lake'
if has_method water.get
then
	check 'water set' "$(ctl water set level=1.5 --node lake -o json)" '1.5'
	check 'water get' "$(ctl water get --node lake -o json)" '"waves"'
	check 'water add-point' "$(ctl water add-point 4 0 width=3 --node lake -o json)" '"index"'
else
	skip 'water'
fi
if has_method mixer.create
then
	mf="${work}/game/audio/main.rmixer"
	check 'mixer create' "$(ctl mixer create "${mf}" 'buses=["music","sfx"]' -o json)" '"sfx"'
	check 'mixer add-bus' "$(ctl mixer add-bus steps parent=sfx path="${mf}" -o json)" '"steps"'
	check 'mixer set-bus' "$(ctl mixer set-bus music volume=-6 path="${mf}" -o json)" '-6'
	check 'mixer effect-types' "$(ctl mixer effect-types -o json)" '"types"'
	check 'mixer save' "$(ctl mixer save path="${mf}" -o json)" 'main.rmixer'
	check 'mixer file' "$(cat "${mf}")" 'steps'
	ctl data close "${mf}" --force >/dev/null 2>&1 || true
else
	skip 'mixer'
fi
if has_method audio.get
then
	ctl create node Speaker --kind audio-emitter >/dev/null
	check 'audio set' "$(ctl audio set Speaker max_distance=30 -o json)" '30'
	check 'audio get' "$(ctl audio get Speaker -o json)" '"min_distance"'
	check 'audio curve' "$(ctl audio curve Speaker samples=4 -o json)" '"points"'
	check 'audio buses' "$(ctl audio buses -o json)" '"buses"'
	ctl undo >/dev/null; ctl undo >/dev/null
else
	skip 'audio'
fi
if has_method tileset.create
then
	tsf="${work}/game/tiles/ground.rtileset"
	check 'tileset create' "$(ctl tileset create "${tsf}" tile_size=16,16 columns=4 tile_count=16 -o json)" 'ground.rtileset'
	check 'tileset tile' "$(ctl tileset tile 3 'flags=["solid"]' path="${tsf}" -o json)" 'solid'
	check 'tileset rule-tile' "$(ctl tileset rule-tile add grass path="${tsf}" -o json)" '"grass"'
	check 'tileset get' "$(ctl tileset get path="${tsf}" -o json)" '"rule_tiles"'
	check 'tileset save' "$(ctl tileset save path="${tsf}" -o json)" 'ground.rtileset'
	ctl data close "${tsf}" --force >/dev/null 2>&1 || true
else
	skip 'tileset'
fi
if has_method sprite.create
then
	spf="${work}/game/sprites/hero.rsprite"
	check 'sprite create' "$(ctl sprite create "${spf}" -o json)" 'hero.rsprite'
	check 'sprite frame add' "$(ctl sprite frame add rect=0,0,16,16 path="${spf}" -o json)" '16'
	check 'sprite clip add' "$(ctl sprite clip add idle 'frames=[0]' path="${spf}" -o json)" '"idle"'
	check 'sprite get' "$(ctl sprite get path="${spf}" -o json)" '"clips"'
	check 'sprite save' "$(ctl sprite save path="${spf}" -o json)" 'hero.rsprite'
	ctl data close "${spf}" --force >/dev/null 2>&1 || true
else
	skip 'sprite'
fi
if has_method tiles.get
then
	check 'tiles state' "$(ctl tiles state --node Floor -o json)" '"grid"'
	check 'tiles brush' "$(ctl tiles brush flip_x=true -o json)" '"mirrored"'
	check 'tiles cell-at' "$(ctl tiles cell-at local=8,8 --node Floor -o json)" '"cell"'
else
	skip 'tiles (rule tiles)'
fi

# 0.9.0 M8 verbs: script debugger (debug), language servers (lsp),
# profiler captures (profiler), frame debugger (frame), tool scripts
# (script), ...;
# each skipped until the editor registers the area's methods.
if has_method debug.state
then
	smoke_lua="${work}/game/scripts/smoke.lua"
	check 'debug state (idle)' "$(ctl debug -o json)" '"state" : "idle"'
	check 'debug set-breakpoint' "$(ctl debug set-breakpoint "${smoke_lua}" 2 condition='1 == 1' -o json)" '"line" : 2'
	check 'debug breakpoints' "$(ctl debug breakpoints -o json)" 'smoke.lua'
	check 'debug toggle-breakpoint (off)' "$(ctl debug toggle-breakpoint 2 path="${smoke_lua}" -o json)" '"set" : false'
	check 'debug toggle-breakpoint (on)' "$(ctl debug toggle-breakpoint 1 path="${smoke_lua}" -o json)" '"set" : true'
	check 'debug add-watch' "$(ctl debug add-watch '1 + 1' -o json)" '"expression" : "1 + 1"'
	check 'debug remove-watch' "$(ctl debug remove-watch '1 + 1' -o json)" '[]'
	check 'debug clear-breakpoints' "$(ctl debug clear-breakpoints -o json)" '"removed" : 1'
	"${ctl}" debug stack >/dev/null 2>&1 && status=0 || status=$?
	check 'debug stack while idle exits 1' "status=${status}" 'status=1'
	check 'debug dap-start' "$(ctl debug dap-start port=0 -o json)" '"port"'
	check 'debug state shows the DAP port' "$(ctl debug state -o json)" '"listening" : true'
	check 'debug dap-stop' "$(ctl debug dap-stop -o json)" '{'
else
	skip 'debug'
fi
if has_method lsp.servers
then
	check 'lsp servers' "$(ctl lsp servers -o json)" '"language"'
	check 'lsp status' "$(ctl lsp status path="${work}/game/scripts/smoke.lua" -o json)" '"language"'
	check 'lsp diagnostics' "$(ctl lsp diagnostics path="${work}/game/scripts/smoke.lua" -o json)" '['
else
	skip 'lsp'
fi
if has_method profiler.set
then
	mkdir -p "${work}/game/captures"
	check 'profiler (bare = sample)' "$(ctl profiler frames=10 -o json)" '"sections"'
	check 'profiler sample' "$(ctl profiler sample -o json)" '"gpu"'
	check 'profiler set' "$(ctl profiler set enable=true ring=64 -o json)" '"ring" : 64'
	sleep 0.3
	check 'profiler capture' "$(ctl profiler capture frames=4 path="${work}/game/captures/smoke.rprof" -o json)" '"threads"'
	check 'profiler capture file' "$(test -s "${work}/game/captures/smoke.rprof" && echo written)" 'written'
	check 'profiler load' "$(ctl profiler load captures/smoke.rprof -o json)" '"frames"'
	check 'profiler frames' "$(ctl profiler frames count=2 -o json)" '"duration_ms"'
	check 'profiler frame' "$(ctl profiler frame -o json)" '"events"'
	check 'profiler view' "$(ctl profiler view mode=flame -o json)" '"mode" : "flame"'
	"${ctl}" profiler compare a=1 >/dev/null 2>&1 && status=0 || status=$?
	check 'profiler compare without b exits 1' "status=${status}" 'status=1'
	ctl profiler set enable=false >/dev/null
else
	skip 'profiler capture'
fi
if has_method frame.status
then
	check 'frame status' "$(ctl frame status -o json)" '"armed"'
	check 'frame capture arms' "$(ctl frame capture -o json)" '"armed" : true'
	"${ctl}" frame call 99999 >/dev/null 2>&1 && status=0 || status=$?
	check 'frame call out of range exits 1' "status=${status}" 'status=1'
else
	skip 'frame'
fi
if has_method script.run
then
	check 'script languages' "$(ctl script languages -o json)" '"id" : "lua"'
	if [[ "$(ctl script languages -o json)" == *'"lua"'*'"available" : true'* ]]
	then
		check 'script run (lua)' "$(ctl script run 'return 1 + 2' -o json)" '3'
		printf 'return 40 + 2\n' >"${work}/game/scripts/tool.lua"
		check 'script run path' "$(ctl script run path=scripts/tool.lua -o json)" '42'
		"${ctl}" script run 'this is not lua' >/dev/null 2>&1 && status=0 || status=$?
		check 'script error exits 1' "status=${status}" 'status=1'
	fi
	check 'script tools' "$(ctl script tools -o json)" '['
	check 'script reload' "$(ctl script reload -o json)" '"loaded"'
	check 'script editor-running' "$(ctl script editor-running -o json)" '['
else
	skip 'script'
fi
if has_method vscript.create
then
	check 'vscript create' "$(ctl vscript create scripts/smoke.rvscript -o json)" '"view" : "graph"'
	check 'vscript get' "$(ctl vscript get scripts/smoke.rvscript -o json)" '"nodes"'
	check 'vscript node-types' "$(ctl vscript node-types query=math.add -o json)" 'math.add'
	check 'vscript validate' "$(ctl vscript validate scripts/smoke.rvscript -o json)" '"ok" : true'
	check 'vscript run' "$(ctl vscript run path=scripts/smoke.rvscript -o json)" 'hello'
	"${ctl}" vscript create scripts/smoke.rvscript >/dev/null 2>&1 && status=0 || status=$?
	check 'vscript create overwrite exits 1' "status=${status}" 'status=1'
	ctl data close scripts/smoke.rvscript --force >/dev/null 2>&1 || true
else
	skip 'vscript'
fi
if has_method memory.status
then
	check 'memory status' "$(ctl memory status -o json)" '"tracked"'
	check 'memory show' "$(ctl memory show -o json)" '"panel" : "memory"'
else
	skip 'memory'
fi
if has_method network.stats
then
	check 'network stats' "$(ctl network stats -o json)" '"enabled"'
	check 'network show' "$(ctl network show -o json)" '"panel" : "network"'
else
	skip 'network'
fi

# 0.8.3: asset sources (none registered without a game module).
check 'source list empty' "$(ctl source list -o json)" '[]'
"${ctl}" source ls nope >/dev/null 2>&1 && status=0 || status=$?
check 'unknown source exits 1' "status=${status}" 'status=1'
"${ctl}" source frobnicate >/dev/null 2>&1 && status=0 || status=$?
check 'source usage error exits 2' "status=${status}" 'status=2'

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
{"jsonrpc":"2.0","id":4,"method":"tools/call","params":{"name":"get_node","arguments":{"node":"nope"}}}
{"jsonrpc":"2.0","id":5,"method":"tools/call","params":{"name":"data_list","arguments":{}}}'
	# Keep stdin open briefly, as real MCP clients do, so every reply is
	# written before the session ends.
	out="$({ printf '%s\n' "${mcp_in}"; sleep 2; } | timeout 20 "${ctl}" mcp 2>&1)"
	check 'mcp initialize' "${out}" '"name":"libregnum-editor"'
	check 'mcp tools/list' "${out}" '"name":"create_node"'
	check 'mcp tilemap tools' "${out}" '"name":"paint_tiles"'
	check 'mcp 0.8.1 tools' "${out}" '"name":"set_asset_meta"'
	check 'mcp code tools' "${out}" '"name":"code_open"'
	check 'mcp data tools' "${out}" '"name":"data_set"'
	check 'mcp palette and preferences tools' "${out}" '"name":"open_preferences"'
	check 'mcp hierarchy_filter' "${out}" '"name":"hierarchy_filter"'
	check 'mcp material tools' "${out}" '"name":"material_set"'
	check 'mcp shader graph tools' "${out}" '"name":"shadergraph_connect"'
	check 'mcp environment tools' "${out}" '"name":"environment_set"'
	check 'mcp post tools' "${out}" '"name":"post_save_look"'
	check 'mcp lod and layer tools' "${out}" '"name":"set_view_layers"'
	check 'mcp bake tools' "${out}" '"name":"bake_start"'
	check 'mcp sequence tools' "${out}" '"name":"sequence_add_key"'
	check 'mcp skel2d tools' "${out}" '"name":"skel2d_paint_weights"'
	check 'mcp anim tools' "${out}" '"name":"anim_add_transition"'
	check 'mcp bonemap and clip tools' "${out}" '"name":"clip_set_morph"'
	check 'mcp physics tools' "${out}" '"name":"physics_set_joint"'
	check 'mcp physics layer tools' "${out}" '"name":"physics_layers_set_collides"'
	check 'mcp physasset tools' "${out}" '"name":"physasset_simulate"'
	check 'mcp physics_layers_get is read-only and takes no params' \
		"$(grep -F '"id":2,' <<<"${out}" | tr -d '\n' | grep -o '"name":"physics_layers_get"[^]]*' | head -n 1)" \
		'"properties":{}},"annotations":{"readOnlyHint":true'
	check 'mcp bake_progress is read-only and takes no params' \
		"$(grep -F '"id":2,' <<<"${out}" | tr -d '\n' | grep -o '"name":"bake_progress"[^]]*' | head -n 1)" \
		'"properties":{}},"annotations":{"readOnlyHint":true'
	check 'mcp 0.8.3 source tools' "${out}" '"name":"source_import"'
	check 'mcp navigation tools' "${out}" '"name":"nav_area_box_add"'
	check 'mcp behaviour tree tools' "${out}" '"name":"bt_add_node"'
	check 'mcp state machine tools' "${out}" '"name":"fsm_add_transition"'
	check 'mcp ai tools' "${out}" '"name":"ai_set_key"'
	check 'mcp particle tools' "${out}" '"name":"particles_add_module"'
	check 'mcp weather tools' "${out}" '"name":"weather_set_param"'
	check 'mcp terrain tools' "${out}" '"name":"terrain_stroke"'
	check 'mcp water tools' "${out}" '"name":"water_add_point"'
	check 'mcp mixer tools' "${out}" '"name":"mixer_set_send"'
	check 'mcp audio tools' "${out}" '"name":"audio_curve"'
	check 'mcp tiles tools' "${out}" '"name":"tiles_refresh"'
	check 'mcp tileset tools' "${out}" '"name":"tileset_rule_tile"'
	check 'mcp sprite tools' "${out}" '"name":"sprite_clip_frame"'
	check 'mcp asset_new' "${out}" '"name":"asset_new"'
	check 'mcp debugger tools' "${out}" '"name":"debug_set_breakpoint"'
	check 'mcp language server tools' "${out}" '"name":"lsp_definition"'
	check 'mcp profiler tools' "${out}" '"name":"profiler_compare"'
	check 'mcp frame debugger tools' "${out}" '"name":"frame_scrub"'
	check 'mcp tool script tools' "${out}" '"name":"script_run_tool"'
	check 'mcp debug_state is read-only and takes no params' \
		"$(grep -F '"id":2,' <<<"${out}" | tr -d '\n' | grep -o '"name":"debug_state"[^]]*' | head -n 1)" \
		'"properties":{}},"annotations":{"readOnlyHint":true'
	check 'mcp nav_get is read-only and takes no params' \
		"$(grep -F '"id":2,' <<<"${out}" | tr -d '\n' | grep -o '"name":"nav_get"[^]]*' | head -n 1)" \
		'"properties":{}},"annotations":{"readOnlyHint":true'
	check 'mcp inspector_state is read-only and takes no params' \
		"$(grep -F '"id":2,' <<<"${out}" | tr -d '\n' | grep -o '"name":"inspector_state"[^]]*' | head -n 1)" \
		'"properties":{}},"annotations":{"readOnlyHint":true'
	check 'mcp data_list' "${out}" '"id":5,"result":{"content":[{"type":"text","text":"[]"}]}'
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
