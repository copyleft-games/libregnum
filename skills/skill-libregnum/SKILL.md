---
name: skill-libregnum
description: >
  Work with libregnum, the GObject/C game engine on raylib (graylib), and
  drive its editor: lrgeditor (level/scene/world editor), lrgeditorctl
  (kubectl-like CLI for running editor sessions), the editor's MCP server
  (lrgeditorctl mcp, lrgeditor --mcp) and its JSON-lines IPC socket.
  Trigger when the user mentions libregnum, lrgeditor, lrgeditorctl,
  .rlevel/.rprefab/project.ryaml files, building or editing levels, scenes
  or worlds for a libregnum game, controlling or scripting the editor,
  or hacking on the libregnum codebase itself.
---

# Skill: libregnum

libregnum is a GObject-based game engine in C (gnu89) built on graylib (a GObject wrapper for raylib) and yaml-glib, with ECS, rendering, audio, UI, scripting, networking/MMO, modding and a full editor. This skill covers two jobs: **using the editor** to build and change game content (levels of nodes saved as `.rlevel` YAML), mostly through `lrgeditorctl`, a kubectl-style CLI that controls a running `lrgeditor` over its socket, or through the editor's MCP tools; and **working on libregnum itself** without tripping over its conventions. This page is the workflow; the details live next to it:

- `references/lrgeditorctl.md`: every command, flag, output format and scripting pattern
- `references/mcp.md`: the MCP server, client setup, and the tool list
- `references/ipc-protocol.md`: the socket protocol under both
- `references/level-format.md`: projects, `.rlevel`/`.rprefab` YAML, validation codes
- `references/engine.md`: building, testing, conventions and traps when changing libregnum's code

## When to Use

- The user wants to create, inspect or change a level, scene, prefab or world in a libregnum project ("add a crate on the table", "line up these lights", "what's in level arena").
- The user mentions `lrgeditor`, `lrgeditorctl`, the libregnum editor, `.rlevel`, `.rprefab` or `project.ryaml`.
- An AI agent should drive the editor (set up or use the MCP server).
- The user wants to script the editor from bash, Emacs or another program (IPC socket).
- Building, testing or changing libregnum's C code (engine, editor, GUI toolkit, tools).

## Prerequisites

- libregnum built from its checkout (`make -j"$(nproc)"`) or installed (`make install`): provides `lrgeditor`, `lrgeditorctl` and `lrgldr` (in `build/release/` when not installed).
- For the editor's MCP server: built with mcp-glib's dependencies (`libsoup3-devel`, `libdex-devel`); `lrgeditorctl mcp` prints a clear error otherwise.
- A running editor for `lrgeditorctl` (windowed `lrgeditor PROJECT`, or `lrgeditor --headless PROJECT` without a display).
- Fedora build packages if building: `gcc make pkgconf glib2-devel json-glib-devel libyaml-devel libdex-devel libsoup3-devel gobject-introspection-devel mesa-libGL-devel libglvnd-devel wayland-devel libxkbcommon-devel libX11-devel libXrandr-devel libXinerama-devel libXcursor-devel libXi-devel sqlite-devel libpq-devel openssl-devel zlib-devel` (plus `luajit-devel python3-devel gjs-devel` for scripting backends). On an immutable host, list these for the image build rather than running `dnf`.

## Instructions

1. **Pick the interface.**
   - A person is (or will be) looking at the editor, or you are in a shell: use `lrgeditorctl` (see `references/lrgeditorctl.md`).
   - You have the libregnum MCP tools available (`editor_info`, `get_tree`, `create_node`, ...): use them; they are the same operations (see `references/mcp.md`).
   - Batch or reviewable changes: write YAML and `lrgeditorctl apply -f`.
   - Changing engine/editor C code: skip to step 8.

2. **Find or start a session.** Run `lrgeditorctl sessions`. If nothing is running:
   - With a display and a person watching: `lrgeditor ~/path/to/project &` (window).
   - Otherwise: `lrgeditor --headless ~/path/to/project &` (IPC only).
   - New project: `lrgeditor --new-project DIR --name "Name"`.
   With several sessions, pass `-s` (PID, socket path, or part of the project/level path) on every call, e.g. `lrgeditorctl -s arena get nodes`.

3. **Look before changing.** `lrgeditorctl get info` (level, dirty flag, counts), `get nodes` (tree as paths), `get node PATH` (full YAML), `get problems`, `get selection`. Nodes are addressed by slash path from the top of the level (`Props/Crate`) or by GUID; paths change when nodes are renamed or moved, GUIDs never do (`-o wide` shows them).

4. **Make changes.** Every change is one undo step in the editor and marks the level dirty.
   - Create: `lrgeditorctl create node Crate --parent Props --primitive cube location=2,0.5,-3 scale=1,1,1`.
   - Update: `lrgeditorctl set node Props/Crate rotation_degrees=0,45,0 visible=true`.
   - Rename / move / duplicate / delete: `rename node P NEW`, `move node P --parent Q [--keep-world]`, `duplicate node P`, `delete node P`.
   - Many nodes at once: write YAML documents (`node:` plus fields; missing nodes are created under the parent in the path) and `lrgeditorctl apply -f file.yaml`.
   - Anything else (components, scripts, prefabs, play mode, camera): `lrgeditorctl run ACTION`, `play`, `view`, or `call METHOD key=value`.
   - Editor 0.8.1 additions: `inspector set NODE NAME section=... value=...` edits a property exactly as the inspector would (several nodes in one undo step); `prefab overrides|apply|revert|sync` manages linked prefab instances (apply overwrites the prefab file: ask first); `meta get|set PATH` reads and changes an asset's `.meta` import settings; `asset rename|move|duplicate|trash PATH` keeps sidecars and GUIDs with their assets (never move asset files with `mv` while an editor runs); data files (input maps, locales, dialog trees, ...) are edited with `data open PATH`, then `data get|set|edit|validate|save` by YAML path (MCP: `data_*` tools).
   - Units: metres; `rotation_degrees` is Euler XYZ in degrees (`rotation` is radians); primitives are 1 m unit shapes, so size them with `scale`.

5. **Verify.** `lrgeditorctl get node PATH`, `get problems` / `validate` (exit 1 on errors). With a windowed editor, `lrgeditorctl screenshot /tmp/v.png [--window]` and look at it (MCP: the `screenshot` tool returns the image); `view layout quad` shows perspective, top, front and right at once and `view render-mode wireframe viewport=1` helps with overlaps. `lrgeditorctl undo` reverts a mistake.

6. **Save when the user wants the change kept.** `lrgeditorctl save` writes the `.rlevel`. Do not save over a level the user did not ask you to change; if unsure, leave it dirty and say so. Never discard unsaved work (`--force` on open/new/quit) unless asked.

7. **Set up MCP when asked to let an agent drive the editor.** `claude mcp add libregnum -- lrgeditorctl mcp` (bridge to whatever editor is running, survives restarts), or `claude mcp add libregnum -- lrgeditor --mcp ~/path/to/project` (the agent launches its own editor; headless without a display). Details and other clients in `references/mcp.md`.

8. **Working on libregnum's code.** Read `AGENTS.md` in the checkout first, then `references/engine.md`. Essentials: gnu89 C, tabs, GNU-style `func (args)` calls, `/* */` comments, gtk-doc/GIR comments in headers (once per identifier), zero warnings (`-Werror`, never silence a warning), no `memcpy` into raylib `Matrix`, raylib calls rely on `-include stdbool.h`; work in a git worktree under `./trees/`; build with `make -j"$(nproc)"`; test with `make test`, plus `make test-editorctl`, `make test-emacs` and `make test-editor-x11` for editor work; update the org docs under `docs/` in the same change; Conventional Commits.

## Workflows (0.9.0)

### Give a level a look
1. Add an `environment:` block to the .rlevel (sky source/path, ibl, ambient, fog, volumetric, `post_stack: look.rpost`) — or use the editor's environment panel.
2. Point nodes at `.rmat` materials (`material: materials/x.rmat`); add `kind: decal` / `kind: reflection-probe` nodes; add `lod-levels: 3` to a model's `.meta` for LOD.
3. Game: `lrg_engine_startup()`, add the level dir as an asset search path, `lrg_level_load_world3d()`, set `LRG_WORLD3D_RENDER_FLAG_POST`, `lrg_world3d_renderer_bake_environment()`, `lrg_world3d_renderer_capture_probes(renderer, world)`; recapture when `lrg_reflection_probe_component_count_pending(world) > 0`.
4. Play-in-editor applies the same look automatically. Reference: examples/pbr-showcase.c.

### Edit a material
1. Create one: `lrgeditorctl call material.create path=materials/crate.rmat shading_model=lit`, or a variant with `material.create_variant path=materials/crate.rmat`.
2. Open and edit: `material.open path=...`, then `material.set path=... key=metallic value=0.8`. Keys are dotted `.rmat` keys: `base_color` ([r,g,b,a]), `textures.albedo` ("tex.png" or "guid:…"), `render_state.blend` or `blend`, `uniforms.<name>`. Each set is one undo step in the material's own history.
3. On an instance, keys the file sets are overrides; `material.revert path=... key=...` makes a key inherit again. `material.get` returns effective values plus `overrides`.
4. `material.save path=...`, then `material.assign node=<guid|name path> path=materials/crate.rmat` (sets the node's `material-ref`, undoable; only primitive and mesh nodes take materials).
5. Custom shading: `shader.create path=shaders/fx.rshader` writes the document plus `fx.frag`; set `shading_model` custom and `shader` "../shaders/fx.rshader"; after editing the `.frag`, `shader.compile path=shaders/fx.frag` and fix the reported `file:line` errors.
6. Node shaders: `shadergraph.create path=shaders/glow.rshadergraph`, add_node/connect/set_param/set_input, `shadergraph.validate`, `shadergraph.save` (writes glow.rshader for a custom material).

### Light and look a level (0.9.0)
1. Lights/volumes: the Create menu (and `lrgeditorctl create node Key --kind light`) has Point Light, Spot Light, Sun, Decal and Reflection Probe; spot `spot_angle` = full cone degrees, `range`; decal params `size_x/y/z`; probe `extents_x/y/z` (half sizes). The inspector edits lights by kind.
2. Environment: `lrgeditorctl env set sky.source gradient`; `env set fog.density 0.02` (dotted block keys or panel field names; one undo step each); `env set post_stack looks/dusk.rpost`. `env bake --wait` needs the window (headless: edits only; the viewport bakes lazily anyway).
3. Look: `post create looks/dusk.rpost` (bloom, tonemap, vignette, fxaa), `post add|move|set|remove`, `post save`, `post assign looks/dusk.rpost`. Opening a .rpost previews it in the viewport (renderer override > world > level); `post look PATH` saves what the viewport shows.
4. Inspect: `view mode overdraw|uv|light-complexity|ao|reflections viewport=N`, `view layers water off`, `lod get NODE`, `lod force NODE 2|auto`.
5. Verify: `screenshot shot.png --window`; the level file stores `environment:` (the game loads the same look).
Traps: an equirect/cubemap sky without `sky.path` bakes the gradient with a warning; LOD force is a shared preview of every instance of the asset; render-layer masks are per viewport and not saved.

### Bake a level's lighting (0.9.0)
1. Mark static geometry: inspector Lighting > Static lighting, or `lrgeditorctl call inspector.set '{"node":"Floor","section":"visual","name":"param-static-lighting","value":true}'`; `param-lightmap-scale` (0 = probe-lit only). Lights: `param-bake-mode` realtime|mixed|baked (baked = direct+bounce in lightmaps; mixed = bounce only).
2. Probe volumes: `lrgeditorctl create node Probes --kind probe-volume location=0,1.5,0`, then `param-extents-x/y/z` (half sizes), `param-spacing` (0 = the bake's probe_spacing) or `param-resolution`.
3. Settings: `lrgeditorctl bake set texels_per_unit 4; lrgeditorctl bake set samples 64` (each an undoable `lighting: settings:` key; `bake reset` drops them).
4. Bake: `lrgeditorctl bake start --wait` (CPU, works headless; exits 1 on failure or cancel). A saved level gets `<level>.rlightmap` (+ .hdr) and `<level>-<n>.rprobes` next to it and its `lighting:` refs (one undo step); an unsaved one keeps the result until `bake save DIR`.
5. Check: the viewport shows the bake while authoring (`bake preview off` compares with realtime); `bake evaluate X Y Z NX NY NZ` reads the light at a point; `screenshot --window`.
Traps: nothing marked static_lighting gives "Nothing to bake"; moving static geometry or duplicating nodes (new GUIDs) needs a re-bake; undo drops the refs but not the files; probe dots stay grey when a volume changed since the bake; straight light/dark lines on walls usually come from a realtime (mixed) light's or the default editor sun's shadows (add a directional light to replace the default sun), not from lightmap charts.

### Animate a character (0.9.0)
1. Import: put the character's `.glb` in the project. Assets panel "Preview Animations..." shows its clips (or `lrgeditorctl clip list chars/hero.glb`; `clip info MODEL CLIP` lists tracks and channels).
2. Graph: "New Animation Graph from Model" on the .glb (or `lrgeditorctl call anim.create path=chars/hero.ranim model=chars/hero.glb`) gives one clip slot and one state per animation. Then `anim.add_parameter {path,name,type:float|int|bool|trigger}`, `anim.add_state {path,name,kind:clip|blend_tree|machine|empty,clip?}`, `anim.set_blend_tree {path,state,type:1d|2d_cartesian|2d_directional|direct,parameter,parameter_y?,children:[{clip,threshold|position:[x,y]}]}`, `anim.add_transition {path,from,to,duration?,exit_time?,conditions:[{parameter,op:'>',value:0.1}]}` (`from:"*"` = any state; a bool/trigger condition is just `{parameter}`); `anim.enter {path,state}` shows a nested machine.
3. Preview: `anim.set_parameter {path,name,value}` (live only; `true` fires a trigger), then `anim.preview {path,advance:0.5}` (returns state per layer and the running transition). `anim.validate`, then `anim.save`.
4. Retarget shared clips: `bonemap.create {path:chars/hero.rbonemap, source:anims/mocap.glb, target:chars/hero.glb, auto_fill:true}`; fix gaps with `bonemap.set {path,source,target|null}` and `bonemap.set_rules {strip_prefixes:["mixamorig:"]}`; `bonemap.get` lists `unmapped_target`; check with `bonemap.preview {path,clip:Walk,time:0.5}`; save.
5. On a node: a mesh-asset node with an `animator3d` component, `graph-path` = the .ranim. The inspector's Animator section shows parameters, layers and IK goals live while playing; viewport overlay Animation > Bones / IK Targets.
Traps: live values aren't defaults (set `default:` in the file); the canvas draws one wire per from->to pair; the editor preview ignores root motion and IK; removing a parameter that conditions use breaks loading until fixed (issues list points at it).

### Make a cutscene (.rseq sequence)
1. Create the actors and cameras in the level (`lrgeditorctl create node Hero --primitive cube`, `... CamA --kind camera`).
2. `lrgeditorctl call sequence.create '{"path":"sequences/intro.rseq","length":8,"fps":24}'` (opens the Sequencer panel; an existing file is refused, use sequence.open).
3. Bind and add tracks: `sequence.bind {"node":"Hero"}`, `sequence.add_track {"kind":"transform","binding":"Hero"}` (kinds: transform, property, camera-cut, audio, event, animation, subsequence; `bone` for bone tracks; `property` + optional `component` for property tracks).
4. Keys: `sequence.add_key {"track":0,"channel":"location.x","time":4,"value":6,"tangent":"broken","in":1.5,"out":0}` (value defaults to the node's current value; tangents auto|user|broken|linear|constant; slopes are value per second). Retime with `sequence.set_key {"track":0,"channel":"location.x","at":4,"time":3.5}` (handles kept).
5. Sections: `sequence.add_section {"track":1,"start":0,"name":"CamA"}` (a camera node name becomes its GUID), events `{"track":2,"start":3.5,"name":"door-open","payload":"slow"}`, audio `name: "bank:sound"`, clips `name: "Walk", duration, rate, loop, weight`.
6. Check it: `sequence.scrub {"time":3.6}` poses the level (not saved, not undoable) and returns `{time, frame, camera, events}`; `sequence.play {"mode":"loop"}` / `{"stop":true}` (stop restores the level). `sequence.get` dumps tracks, keys with resolved slopes, sections.
7. `sequence.save {}`. In the game: `LrgSequenceComponent` (component `sequence`, params `sequence_path`, `autoplay`) or `LrgSequencePlayer`.
Traps: the sequence has its own undo (`sequence.undo`), separate from Ctrl+Z; opening another file drops unsaved edits; bindings are GUIDs (use override names like `self` for reusable sequences); audio/animation/sub-sequence/component-property tracks do not preview on level nodes; a level timeline converts with `sequence.from_timeline {"timeline":"Bounce","path":"sequences/bounce.rseq"}`. Level timeline keys can be Bézier keys: `timeline.tangent` (or the Timeline panel's Tangent picker); moving a key keeps its handles; picking an easing makes it eased again; the Bone field + Key Bone keys `bone.NAME.*` offsets.

### Rig a 2D character (0.9.0)
1. Make a rig: `lrgeditorctl call skel2d.create path=rigs/hero.rskel2d name=hero` (one `root` bone; never overwrites), then `skel2d.open path=...` (or double-click the .rskel2d). Every skel2d.* call takes `path`; each edit is one undo step in the rig's own history (`data undo`), never the level's.
2. Bones (setup space: x right, y DOWN, degrees CLOCKWISE; x/y/rotation relative to the parent; length along local +X): `skel2d.add_bone path=... name=torso parent=root rotation=-90 length=80` (rotation -90 points up). Fix with `skel2d.set_bone bone=torso x=.. rotation=.. length=.. name=..` (a rename fixes every reference); `skel2d.remove_bone` also removes descendants, their slots, weights and timelines.
3. Images: `skel2d.add_slot name=torso bone=torso`, then `skel2d.attach slot=torso image=art/torso.png x=-35 y=-108 rows=3` (x/y = top-left in SETUP space; size defaults to the image size; columns/rows > 1 makes a deformable grid; image stored relative to the rig). In the editor, dropping an image from Assets on the canvas does the same on the selected slot or bone.
4. Weights: `skel2d.auto_weights slot=torso name=torso bones='["hip","torso"]'` (falloff 4, 2 influences); touch up with `skel2d.paint_weights slot=.. name=.. bone=.. x=.. y=.. radius=20 strength=0.5 [erase=true]`. Mesh: `skel2d.mesh slot=.. name=.. add='[[x,y]]' triangulate=true` (UVs fitted automatically).
5. Draw order = slots list, back to front: `skel2d.draw_order order='["leg_l","torso","arm_r"]'`. Skins: `skel2d.skin name=gold op=add` (becomes active; new attachments go into the active skin; lookups fall back to default).
6. Animate: `skel2d.clip name=wave op=add duration=1.2 loop=true`, then `skel2d.key target=fore_r channel=rotate time=0.6 values='[60]' curve='[0.42,0,0.58,1]'`. Bone values are OFFSETS from the setup pose (rotate added, translate added, scale multiplied); color/attachment keys target slots. `skel2d.play clip=wave time=0.6` poses the preview; `skel2d.get` reads everything back.
7. `skel2d.save path=...`; check with `screenshot shot.png --window`. Play it in a game with LrgSkeleton2DComponent (`lrg_skeleton2d_component_load` + `_play`).
Traps: a positive rotation turns clockwise on screen; rotation keys are not wrapped (350 -> 10 spins backwards); a draw-order key must name every slot; textures only show with a window (headless editors still edit everything).

### Add physics to a level (0.9.0)
1. Static geometry: a `physics:` block with `body: static` and `box`/`mesh` shapes (mesh without `mesh:` uses the node's model).
2. Props: `body: dynamic`, a mass, and a box/convex/sphere shape; `trigger: true` for volumes.
3. Joints go on the child node with `target:` = the other node's GUID.
4. Layer names and `no_collide` pairs go in the `project.ryaml` root `physics:` block.
5. In the game `lrg_level_load_world3d()` attaches physics; just step the world. (Editor gizmos, inspector and layer matrix arrive with the M5 editor tools; until then edit the block in the `.rlevel`.)

### Edit physics in the editor (0.9.0)
1. Select the node, open the Physics panel (`lrgeditorctl run physics.show` or View > Panels > Physics).
2. Add a body: `lrgeditorctl call physics.add_body '{"node":"Crate","kind":"dynamic"}'` (comes with a shape fitted to the geometry: sphere for spheres, cylinder for cylinders/cones, convex hull (dynamic) or triangle mesh (static/kinematic) for mesh assets, a box otherwise).
3. Set fields with `physics.set_body` / `physics.set_shape` / `physics.set_joint` `{"index":i,"values":{...}}`: field names are the LrgNodePhysics / LrgNodePhysicsShape / LrgNodePhysicsJoint property names; enums are nicks; vectors `[x,y,z]` (half-extents, offset, rotation in degrees, anchor, axis). Joint `target` is a node path or GUID, "" = the world. Limits are radians for hinge/cone/swing-twist, metres for slider/distance.
4. `physics.add_shape {"kind":"sphere","values":{"radius":2,"trigger":true}}`; `physics.add_joint {"kind":"hinge","target":"Frame","values":{...}}`; `physics.fit {"index":0,"kind":"capsule"}`. Every call is one undo step; check with `physics.get`.
5. Show physics: `physics.overlay '{"flags":["shapes","joints"]}'` (shapes, aabbs, contacts, joints, sleeping, com, velocity, broadphase, all, none). Play (`lrgeditorctl play start`) and read the live body with `physics.state`.
6. In the viewport drag the selected node's collider handles (face centres, radius, cap tip, joint anchor): one undo step per drag, Escape cancels. 2D nodes use a `collider` component (shape box|circle, width, height, radius, offset_x, offset_y), with handles too.
Part 2: collision layers, world settings and ragdolls.
- Layers and world settings are project data in the project.ryaml `physics:` block (32 named layers, symmetric collide matrix, `gravity`, `fixed_dt`, `max_substeps`, `iterations`, `sleep_threshold`, `sleep_substeps`). UI: File > Project Settings > Physics (matrix upper triangle, click toggles; applied on Save). Script: `lrgeditorctl physics layers [get]`, `physics layers name 9 water` (`-` unnames), `physics layers collide water debris off`, `physics layers settings gravity=0,-3.7,0 iterations=12`, `physics layers reset`, `physics layers open`. These IPC writes save project.ryaml immediately and cannot be undone.
- Ragdolls (.rphysasset): one part per bone (capsule/box/sphere, offset, rotation, mass) with a joint to its parent part (swing-twist, cone, hinge, point, fixed; limits in radians) plus exclusions. Create: right-click a skinned .glb > New Physics Asset from Model, or `lrgeditorctl physasset create chars/hero.rphysasset model=chars/hero.glb total_mass=80`. Tune: `physasset set-part PATH shin_l joint=hinge twist_degrees=0,140`, `physasset set-part PATH head shape=sphere radius=0.12`, `physasset exclude PATH thigh_l thigh_r`, `physasset stabilize PATH`. Try: `physasset simulate PATH advance=2` (passive drop; `run=false` returns to bind pose); `physasset save PATH`. Each edit is one undo step of the file (`data undo PATH`). The preview model lives in the `.meta` sidecar (`physasset model PATH chars/hero.glb`). Keys must match the shape and joint kind (no `radius` on a box, no `twist` on a cone).

## Output Format

- For editor work: report what changed as `node/PATH created|configured|deleted` lines (what `lrgeditorctl` prints), whether the level is saved or still dirty, and any problems from `get problems`.
- For YAML the user can keep or re-apply, write `.yaml` files in the shape `lrgeditorctl get node PATH -o yaml` prints (one document per node, `---` between them).
- For code changes: normal commits in the libregnum repo with docs updated alongside.

## Examples

### Example: Furnish a room from the shell

**Input:** "In my game at ~/games/keep, put a table in the middle of the hall with two chairs, and a lamp above it."

**Result:**

```bash
lrgeditorctl sessions || true                       # nothing running? start one:
lrgeditor --headless ~/games/keep >/dev/null 2>&1 &
sleep 1
lrgeditorctl get nodes                               # find the hall: Hall
cat > /tmp/furnish.yaml <<'EOF'
node: Hall/Table
primitive: cube
location: [0, 0.4, 0]
scale: [2, 0.8, 1]
---
node: Hall/ChairA
primitive: cube
location: [-1.4, 0.25, 0]
scale: [0.5, 0.5, 0.5]
---
node: Hall/ChairB
primitive: cube
location: [1.4, 0.25, 0]
scale: [0.5, 0.5, 0.5]
---
node: Hall/Lamp
kind: light
location: [0, 3, 0]
EOF
lrgeditorctl apply -f /tmp/furnish.yaml
# node/Hall/Table created
# node/Hall/ChairA created
# node/Hall/ChairB created
# node/Hall/Lamp created
lrgeditorctl validate && lrgeditorctl save
# No problems.
# saved /home/me/games/keep/levels/main.rlevel
```

### Example: Same task through MCP tools

**Input:** "Using the libregnum MCP tools, add a red sphere on top of Props/Crate."

**Result:** call `get_node {"node":"Props/Crate"}` (location [2,0.5,-3], scale [1,1,1] so its top is y=1.0), then `create_node {"name":"Ball","parent":"Props","primitive":"uv-sphere","location":[2,1.5,-3]}`, then `update_node {"node":"Props/Ball","visual":{"material":{"color":[0.9,0.1,0.1,1]}}}`, then `screenshot {}` to check it, then report and ask whether to `save_level`.

### Example: Edit one node interactively

**Input:** "Let me tweak the camera by hand."

**Result:** `lrgeditorctl edit node Camera` opens the node as YAML in `$VISUAL`/`$EDITOR`; saving the file applies the change (one undo step) and prints `node/Camera edited`; quitting without changes prints `Edit cancelled, no changes made.`

## Constraints

- Never run `lrgeditorctl quit --force`, `open --force` or `new --force`, or the `open_level`/`new_level` MCP tools with force, unless the user explicitly asked to discard unsaved changes.
- Do not save levels the user did not ask to change; say when a level is left dirty.
- Prefer node paths for readability but use GUIDs (`-o wide`) when renaming/moving several nodes in one script, since paths change.
- Do not hand-edit an `.rlevel` file while an editor has the same level open with unsaved changes; use `lrgeditorctl` (or save first; the editor reloads clean levels changed on disk).
- On immutable hosts do not install packages with `dnf` or run `bootc usr-overlay`; list the Fedora packages instead.
- When changing libregnum code: zero warnings, no `//` comments, no silencing warnings, docs in org-mode under `docs/`, commit only when asked.

## Play-in-editor (0.9.0)

Play uses 3D when the level's `default_view` is `3d` (the default) and 2D otherwise. During 3D play the viewport draws the runtime world (the same renderer games use). The hierarchy Live tab shows the entity tree; the inspector shows the entity's transform and components, and "Apply to Level" writes component values back. Simulated transforms sync back field by field and are restored on Stop.
