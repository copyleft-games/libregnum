# M6/M7 editor wave: navigation, AI, VFX, terrain, audio and 2D tools in lrgeditor (read with brief.md and ../v081/brief-w4.md)

The M6/M7 engine is COMMITTED (`git log --oneline 4c85b52..HEAD`). Read the engine docs for your area
first; every engine report listed "hooks for the editor" and those hooks exist:
- NAV: docs/modules/pathfinding/{nav-bake,nav-agent,nav-poly-mesh}.org, docs/guides/navigation.org
  (LrgNavBake form, bake/bake_into, lrg_level_set_nav_*, lrg_world3d_debug_draw_navigation,
  lrg_nav_crowd_set_debug_agent, get_last_step_seconds).
- AI: docs/modules/ai/{bt-asset,logic-machine,ai-component}.org, docs/guides/ai.org (LrgBtAssetNode
  model, type/binding palettes, validate/dup_unbound, `editor:` block kept on save, LrgBtRunner
  get_node_status/dup_active_path, FSM signals + dup_debug_state, LrgDecisionBrain topic API).
- VFX: docs/modules/particles/{particle-asset,ribbon-renderer,mesh-renderer,gpu-particles}.org,
  docs/modules/weather/weather-asset.org, docs/guides/vfx.org (module GObjects with property hints,
  list_kinds, changed signals, effect seek/simulate/reload, get_gpu_capable, weather list_kind_params,
  sync_weather).
- TERRAIN: docs/modules/graphics/{terrain-asset,terrain-brush,foliage,water-body}.org, guides/terrain.org
  (LrgTerrainBrush begin_stroke/apply/end_stroke -> LrgTerrainEdit undo, lrg_terrain_component_raycast,
  node kinds LRG_NODE_VISUAL_TERRAIN / _WATER with `asset` param).
- AUDIO: docs/modules/audio/mixer-asset.org, guides/audio.org (.rmixer, meters API, snapshots,
  attenuation curves/cones on LrgAudioSourceComponent).
- 2D: docs/modules/tilemap/{rule-tile,tileset-asset,iso-hex}.org, docs/modules/atlas/*, guides/2d.org.

Patterns to copy: M5 physics panel (src/editor/ui/lrg-editor-physics-panel.c: docked panel, every edit
one undoable level command, live state while playing), physics asset editor
(src/editor/ui/data/lrg-editor-physics-asset-editor.c: data editor with a live 3D preview), material
editor, sequencer panel, gizmo-paint volume handles. Every tool registers its own actions and IPC
methods from its own file (brief-w4.md). Screenshots: AGENTS.md > Testing > GPU tests, editor runs
and screenshots (build-aux/xrun.sh, 1920x1080, dark AND light), LOOK at them, keep them in
`/tmp/claude-1000/-var-home-zach-source-projects-libregnum/c474e1e8-b6e5-4b4b-bd42-fe50c0433c3f/scratchpad/v090/shots/m67e-<agent>-*.png`.

Shell files (src/editor/ui/lrg-editor-app.c, lrg-editor-window.c, lrg-editor-app-actions.c,
lrg-editor-app-private.h) stay the LEAD's: docking a panel, menu entries (Create > Terrain/Water,
Window > ...) go in your report as exact patches. The lead docks panels when committing.

## Shared-file protocol (seven agents, read carefully)
- `.../scratchpad/v090/m67e-ipc/<AGENT>.md`: write your IPC method list there EARLY (name, params,
  result, errors, one line each) and keep it current. EDPLUMB turns them into lrgeditorctl verbs, MCP
  tools, smoke tests and the IPC docs.
- `.../scratchpad/v090/m67e-patches/<target-file-basename>.<AGENT>.patch`: a small hook patch for a
  file another agent owns (`diff -u`, against the current file). The owner applies every patch for
  its files when it sees one (check the dir whenever you rebuild) and deletes nothing: rename applied
  ones to `*.applied`. Keep these to hook calls (one function call / one menu row), never logic.
- Owners: viewport-panel.c = EDTERR; gizmo-paint + lrg-editor-gizmo.c + inspector-panel.c = EDAUDIO;
  hierarchy-panel.c = EDNAV; lrg-editor-document.{c,h}, data-editor.c, assets-panel.{c,h}, editorctl,
  ipc-protocol.org, lrgeditorctl.org = EDPLUMB; tiles-panel.c = ED2D.

## EDPLUMB (first: formats, then CLI/MCP)
Phase 1 (do this FIRST, within your first hour, then write `m67e-ipc/FORMATS-READY`):
src/editor/lrg-editor-document.{c,h} (append formats BT ".rbt", FSM ".rfsm", PARTICLES ".rparticles",
WEATHER ".rweather", MIXER ".rmixer", TILESET ".rtileset", SPRITE ".rsprite", WATER ".rwater", NAV_SETTINGS
".rnav" (the document holds the engine object: load/save through the engine's own loaders/writers, so
round trips stay byte-stable; undo snapshots via to_data/copy)), src/editor/ui/data/lrg-editor-data-editor.c
(builtins: BT -> LrgEditorBtEditor, FSM -> LrgEditorFsmEditor, PARTICLES -> LrgEditorParticleEditor,
WEATHER -> LrgEditorWeatherEditor, MIXER -> LrgEditorMixerEditor, TILESET -> LrgEditorTilesetEditor,
SPRITE -> LrgEditorSpriteEditor, WATER -> LrgEditorWaterEditor; NAV_SETTINGS opens the nav panel through
an action `nav.open-settings` that EDNAV registers), src/editor/ui/lrg-editor-assets-panel.{c,h}
(glyphs, type names, New entries for every format, "New Particle Effect", "New Behaviour Tree", "New
State Machine", "New Weather", "New Mixer", "New Tileset", "Slice Sprite Sheet" on .png -> .rsprite,
"New Water Body", "New Terrain" (writes a flat .rterrain with sidecars via LrgTerrainAsset)),
tests: extend tests/test-editor-document.c (load/save/undo for each new format).
Phase 2: src/editorctl/** (lrgeditorctl verbs + MCP tools for EVERY method in m67e-ipc/*.md, the same
way the physics/bake verbs were done), tests/editorctl-smoke.sh (one pass per area), docs
docs/editor/ipc-protocol.org sections, docs/editor/lrgeditorctl.org sections, the skill references
(skills/skill-libregnum/references/{ipc-protocol,lrgeditorctl,mcp}.md: you are granted these three
files). Gate: `make test-editorctl`, `make test-emacs`.

## EDNAV
Own: src/editor/ui/lrg-editor-nav-panel.{c,h} (stub; docked "Navigation" panel: the level's
`navigation:` block (settings .rnav picker/new, data path, bake_on_load), the LrgNavBake form (agent,
voxel, regions, sources/filters), area types list (name, tag, cost, colour) with box volumes, off-mesh
links list, Bake (progress, cancel if feasible, last triangle count/time), Save; every edit undoable;
while playing: agent list of the world's crowd with state/path/remaining distance, select one ->
lrg_nav_crowd_set_debug_agent, step time), the viewport navigation overlay (a draw hook function in
your panel + a patch for viewport-panel.c's overlay menu: polygons by area, links, boundaries, agent
paths, ORCA for the debug agent), area-box and link endpoint handles (patch to gizmo-paint via the
protocol, or draw/drag them from your hook if the viewport exposes a tool hook),
src/editor/ui/lrg-editor-hierarchy-panel.c (badge on nodes with a nav-agent component),
tests/test-editor-app-nav.c, docs/editor/navigation.org (new). IPC `nav.*` (get, set, bake, save,
areas/links add/remove/set, overlay flags, agents (while playing), debug-agent).

## EDAI
Own: src/editor/ui/data/lrg-editor-bt-editor.{c,h} (stub; node-graph editor for .rbt on the editor's
LrgNodeGraph widget: palette of composite/decorator/leaf types + registered actions/conditions
(binding palette), node params via property grid, blackboard schema table, subtree refs (open in a
tab), validate -> problems with node ids, auto layout + keep `editor:` positions, live state while
playing (the selected entity's LrgAiComponent runner: node status colours, active path, ticks)),
src/editor/ui/data/lrg-editor-fsm-editor.{c,h} (stub; state graph for .rfsm: states (hierarchical:
double-click to enter a sub-machine, breadcrumb), transitions with event/after/when/do editors,
enter/update/exit action lists, blackboard schema, live state highlight + transition fire counts),
src/editor/ui/lrg-editor-ai-debug-panel.{c,h} (stub; docked "AI" panel while playing: entities with
LrgAiComponent, blackboard live table (editable), current node/state, send event; the LLM decisions
inspector: LrgDecisionBrain topics, last request JSON, latency, pending, counts, totals, `asked` log),
tests/test-editor-app-ai.c, docs/editor/ai.org (new). IPC `bt.*`, `fsm.*`, `ai.*`.

## EDFX
Own: src/editor/ui/data/lrg-editor-particle-editor.{c,h} (stub; .rparticles editor: emitter list
(add/remove/reorder, solo/mute via enabled), module stack per emitter grouped by stage (add from
list_kinds, remove, reorder, enable), module properties through the property grid (curves, gradients,
ranges, colours with their hints), renderer block, live 3D preview (LrgParticleEffect, play/pause/
restart, timeline scrub with seek, speed, seed, ground plane, background), stats (active count per
emitter, bounds), GPU toggle with the check_emitter message, unknown-keys warning),
src/editor/ui/data/lrg-editor-weather-editor.{c,h} (stub; .rweather editor: presets list, layers with
kind-specific param forms from list_kind_params, intensities, transition rules, live preview scene with
sync_weather and a "transition to" preview), tests/test-editor-app-vfx.c, docs/editor/vfx.org (new).
IPC `particles.*`, `weather.*`.

## EDTERR
Own: src/editor/ui/lrg-editor-terrain-panel.{c,h} (stub; docked "Terrain" panel for the selected
terrain node: tools (raise/lower/flatten/smooth/noise/paint/hole/fill/foliage paint/erase), radius,
strength, falloff/curve, target height + sample, paint layer picker with layer list (add/remove,
texture/material/tiling/colour), foliage layers list with their rules, chunk grid info, Rebake),
src/editor/ui/data/lrg-editor-water-editor.{c,h} (stub; .rwater editor: shape, level, waves list,
colours, optics, foam, with a live preview; in-level lake/river points editable through viewport
handles), src/editor/ui/lrg-editor-viewport-panel.{c,h} (brush tool: cursor ring projected on the
terrain via lrg_terrain_component_raycast, strokes while dragging, ONE undo command per stroke from the
LrgTerrainEdit, Ctrl inverts, Shift smooths, [ ] radius; water point handles; apply other agents'
overlay-menu patches), tests/test-editor-app-terrain.c, docs/editor/terrain.org (new). IPC
`terrain.*` (get, stroke (scripted strokes for tests/agents), layers, foliage layers, save),
`water.*`. Create menu Terrain/Water + node kinds rendering in the editor scene renderer: the scene
renderer must draw TERRAIN/WATER nodes while authoring (src/editor/ui/lrg-editor-scene-renderer.c is
yours this wave for that).

## EDAUDIO
Own: src/editor/ui/data/lrg-editor-mixer-editor.{c,h} (stub; .rmixer editor: channel strips per bus
(volume fader, mute, solo, parent routing, sends with levels, effect chain with add/remove/reorder and
params), snapshots (capture, blend time, preview blend), ducking rules), src/editor/ui/lrg-editor-mixer-panel.{c,h}
(stub; docked "Mixer" panel: the project's default mixer (project.ryaml reference) with live peak/RMS
meters while playing or previewing, a compact strip view embedding the mixer editor's strips),
src/editor/ui/lrg-editor-gizmo-paint.{c,h} + src/editor/lrg-editor-gizmo.c (attenuation gizmo for
audio emitters: min/max distance spheres with drag handles, cone inner/outer angles; apply other
agents' gizmo patches), src/editor/ui/lrg-editor-inspector-panel.c (audio source section: curve kind,
custom curve editor, min/max, cone, doppler, occlusion, bus picker), tests/test-editor-app-audio.c,
docs/editor/audio.org (new). IPC `mixer.*`, `audio.*`.

## ED2D
Own: src/editor/ui/lrg-editor-tiles-panel.{c,h} (rule tiles in the palette, paint with rule tiles
(autotile on paint + "Refresh all"), flip/rotate bits while painting (X/Y/Z keys), iso/hex grid
support for picking and the grid overlay in 2D levels, tile property display), src/editor/ui/data/
lrg-editor-tileset-editor.{c,h} (stub; .rtileset editor: tile grid view, per-tile properties (custom
data), collision polygon editor (draw/drag points), animation frames, rule tile editor: 3x3 neighbour
masks clicked per tile, 47/16 presets, random variants with weights), src/editor/ui/data/
lrg-editor-sprite-editor.{c,h} (stub; .rsprite slicer + frame animation editor: grid or explicit rects
over the texture, pivots, named frames, animations (fps, loop, events) with playback preview),
tests/test-editor-app-2d.c, docs/editor/2d-tools.org (new). IPC `tileset.*`, `sprite.*`, `tiles.*`
additions.

## Gate (each agent)
`make all` 0 warnings (GIR), your suites + every test-editor-app* you touch + test-editor-app green
(GPU under build-aux/xrun.sh), docs-check clean for your headers, dark+light 1920x1080 screenshots
looked at. Skill text (SKILL.md section for your tools) in your report (EDPLUMB edits the three
reference files itself). Report: files, IPC list, shell patches, patches you applied for others,
screenshots, anything not done.
