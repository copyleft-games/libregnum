---
name: skill-libregnum
description: >
  Work with libregnum, the GObject/C game engine on raylib (graylib), and
  drive its editor: lrgeditor (level/scene/world editor), lrgeditorctl
  (kubectl-like CLI for running editor sessions), the editor's MCP server
  (lrgeditorctl mcp, lrgeditor --mcp) and its JSON-lines IPC socket.
  Trigger when the user mentions libregnum, lrgeditor, lrgeditorctl,
  .rlevel/.rprefab/project.ryaml files, building or editing levels, scenes
  or worlds for a libregnum game, authoring engine assets in the editor
  (materials, animation, physics, navigation, AI, VFX, terrain, audio,
  2D, visual scripts), composing songs (.rsong) or sound effects
  (.rfoley), controlling or scripting the editor, or hacking on the
  libregnum codebase itself.
---

# Skill: libregnum

libregnum is a GObject-based game engine in C (gnu89) built on graylib (a GObject wrapper for raylib) and yaml-glib, with ECS, rendering, audio, UI, scripting, networking/MMO, modding and a full editor. This skill covers two jobs: **using the editor** to build and change game content (levels of nodes saved as `.rlevel` YAML), mostly through `lrgeditorctl`, a kubectl-style CLI that controls a running `lrgeditor` over its socket, or through the editor's MCP tools; and **working on libregnum itself** without tripping over its conventions. This page is the workflow; the details live next to it. Current as of libregnum 0.10.2.

- `references/lrgeditorctl.md`: every command, flag, output format and scripting pattern
- `references/mcp.md`: the MCP server, client setup, and the tool list
- `references/ipc-protocol.md`: the socket protocol under both
- `references/level-format.md`: projects, `.rlevel`/`.rprefab` YAML, validation codes
- `references/engine.md`: building, testing, conventions and traps when changing libregnum's code
- `references/workflows.md`: step-by-step recipes for every 0.9.x/0.10.x authoring tool (look, materials, bake, animation, sequences, 2D rigs, physics, navigation, AI, VFX, terrain, audio, 2D tiles, debugging/profiling, visual scripts, scenes/gizmo/VCS/collab)
- `references/formats.md`: engine YAML formats (.rmat, .ranim, .rbt, .rnav, .rparticles, .rsong, .rfoley, ...)
- `references/music.md`: composing songs and sound effects through the editor (song.*, foley.*): discovery, `song.apply` batches, mixing by render numbers, export

## When to Use

- The user wants to create, inspect or change a level, scene, prefab or world in a libregnum project ("add a crate on the table", "line up these lights", "what's in level arena").
- The user mentions `lrgeditor`, `lrgeditorctl`, the libregnum editor, `.rlevel`, `.rprefab` or `project.ryaml`.
- An AI agent should drive the editor (set up or use the MCP server).
- The user wants to script the editor from bash, Emacs or another program (IPC socket).
- The user wants to author a material, animation graph, cutscene, ragdoll, navmesh, behaviour tree, particle effect, terrain, mixer, tileset, visual script, song or sound effect for a libregnum game.
- Building, testing or changing libregnum's C code (engine, editor, GUI toolkit, music/audio, tools).

## Prerequisites

- libregnum built from its checkout (`make -j"$(nproc)"`) or installed (`make install`): provides `lrgeditor`, `lrgeditorctl` and `lrgldr` (in `build/release/` when not installed).
- For the editor's MCP server: built with mcp-glib's dependencies (`libsoup3-devel`, `libdex-devel`); `lrgeditorctl mcp` prints a clear error otherwise.
- A running editor for `lrgeditorctl` (windowed `lrgeditor PROJECT`, or `lrgeditor --headless PROJECT` without a display).
- Fedora build packages if building: `gcc make pkgconf glib2-devel json-glib-devel libyaml-devel libdex-devel libsoup3-devel gobject-introspection-devel mesa-libGL-devel libglvnd-devel wayland-devel libxkbcommon-devel libX11-devel libXrandr-devel libXinerama-devel libXcursor-devel libXi-devel sqlite-devel libpq-devel openssl-devel zlib-devel` (plus `luajit-devel python3-devel gjs-devel` for scripting backends; the libav `-free-devel` set (`libavformat libavcodec libavutil libswscale libswresample`) for `FFMPEG=1`, the in-process video decoder and audio encoder; without it, MP3/AAC/Opus/... export needs the `ffmpeg` program on `PATH`). On an immutable host, list these for the image build rather than running `dnf`.

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
   - Engine assets (0.9.0+): `lrgeditorctl asset new KIND` (`bt fsm particles weather mixer tileset sprite water terrain nav-settings vscript song foley`), then the family verbs in `references/workflows.md`.
   - Units: metres; `rotation_degrees` is Euler XYZ in degrees (`rotation` is radians); primitives are 1 m unit shapes, so size them with `scale`.

5. **Verify.** `lrgeditorctl get node PATH`, `get problems` / `validate` (exit 1 on errors). With a windowed editor, `lrgeditorctl screenshot /tmp/v.png [--window]` and look at it (MCP: the `screenshot` tool returns the image); `view layout quad` shows perspective, top, front and right at once and `view render-mode wireframe viewport=1` helps with overlaps. `lrgeditorctl undo` reverts a mistake.

6. **Save when the user wants the change kept.** `lrgeditorctl save` writes the `.rlevel`. Do not save over a level the user did not ask you to change; if unsure, leave it dirty and say so. Never discard unsaved work (`--force` on open/new/quit) unless asked.

7. **Set up MCP when asked to let an agent drive the editor.** `claude mcp add libregnum -- lrgeditorctl mcp` (bridge to whatever editor is running, survives restarts), or `claude mcp add libregnum -- lrgeditor --mcp ~/path/to/project` (the agent launches its own editor; headless without a display). Details and other clients in `references/mcp.md`.

8. **Working on libregnum's code.** Read `AGENTS.md` in the checkout first, then `references/engine.md`. Essentials: gnu89 C, tabs, GNU-style `func (args)` calls, `/* */` comments, gtk-doc/GIR comments in headers (once per identifier), zero warnings (`-Werror`, never silence a warning), no `memcpy` into raylib `Matrix`, raylib calls rely on `-include stdbool.h`; work in a git worktree under `./trees/`; build with `make -j"$(nproc)"`; test with `make test`, plus `make test-editorctl`, `make test-emacs` and `make test-editor-x11` for editor work; update the org docs under `docs/` in the same change; Conventional Commits. Tests never touch the desktop: `tests/lrg-test-guard.c` sets `LRG_EXTERNAL_EDITOR=none` in every test binary (do not set it yourself), GPU/editor runs go through `build-aux/xrun.sh`, and nothing in a test opens the audio device.

## Workflows (index)

Step-by-step recipes live in `references/workflows.md` (music in `references/music.md`). Pick the one that matches the request, read it, then use the verbs it names:

| Task | Section in `references/workflows.md` | Key verbs / methods |
|------|------|------|
| Level look: sky, fog, IBL, post stack, LOD, view modes | Give a level a look; Light and look a level | `env set`, `post create`, `view mode`, `lod force` |
| Materials, shaders, shader graphs | Edit a material | `material.*`, `shader.compile`, `shadergraph.*` |
| Baked lightmaps and probe volumes | Bake a level's lighting | `bake set`, `bake start --wait` |
| Character animation graphs, retargeting | Animate a character | `anim.*`, `bonemap.*`, `clip list` |
| Cutscenes (`.rseq`) | Make a cutscene | `sequence.*` |
| 2D skeletal rigs (`.rskel2d`) | Rig a 2D character | `skel2d.*` |
| Physics bodies, joints, layers, ragdolls | Add physics / Edit physics in the editor | `physics.*`, `physics layers`, `physasset` |
| Navigation meshes and agents | Add navigation / Bake navigation in the editor | `nav.*` |
| Behaviour trees, state machines, AI debugging | Behaviour trees, state machines and AI debugging | `bt`, `fsm`, `ai` |
| Particles and weather | Author VFX in the editor | `particles.*`, `weather.*` |
| Terrain, foliage, water | Terrain and water | `terrain.stroke`, `water.set` |
| Mixer buses, effects, snapshots, 3D emitters | Mix and spatialise audio | `mixer.*`, `audio.set` |
| Tiles, rule tiles, sprite sheets | 2D tools | `tiles.*`, `tileset.*`, `sprite.*` |
| Debugger, LSP, profiler, frame debugger, memory/network, tool scripts | Debug, profile and tool scripts | `debug.*`, `lsp.*`, `profiler.*`, `frame.*`, `memory.*`, `network.*`, `script.*` |
| Visual scripts (`.rvscript`), blueprint canvas | Visual scripts: the blueprint canvas | `vscript ...`, `vscript frame`, `vscript palette` |
| Export packs, locales, merge, scenes, gizmo, VCS, collaboration | Packs, locales and level merge; Scenes, gizmo, locales, version control and collaboration | `export.run`, `po`, `merge`, `scenes`, `gizmo`, `vcs`, `collab` |
| Songs and sound effects (`.rsong`, `.rfoley`) | `references/music.md` | `song.apply`, `song.render`, `song.export`, `foley.*` |
| New engine-backed asset of any kind | Engine-backed assets | `asset new KIND` |

Common rules across these tools: every edit is one undo step (file documents: `data undo PATH`; level edits: `undo`); saves write the engine's canonical bytes (YAML comments in those files are not kept); textures, bakes that need GL, and screenshots need a windowed editor; never call `song.play` / `foley.play` / `play start` in CI unless asked. Play-in-editor uses the same runtime renderer games use (section "Play-in-editor" in `references/workflows.md`).


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
- Never `vcs commit`, `prefab apply`, `physics layers ...` (saves project.ryaml at once) or `merge` into the user's files without asking.
- When changing libregnum code: zero warnings, no `//` comments, no silencing warnings, docs in org-mode under `docs/`, commit only when asked.
