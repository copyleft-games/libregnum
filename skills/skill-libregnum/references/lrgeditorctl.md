# lrgeditorctl reference

`lrgeditorctl` controls running `lrgeditor` sessions the way kubectl controls a cluster: `lrgeditorctl [flags] VERB [RESOURCE] [NAME...] [key=value...]`. It talks to the editor's JSON-lines IPC socket (one connection per call, so editor restarts are harmless), starts instantly, and needs no display.

## Sessions

Editors listen on `$XDG_RUNTIME_DIR/libregnum/editor-PID.sock` (or `--socket` / `$LRG_EDITOR_SOCKET`). `lrgeditorctl sessions` lists live ones (stale sockets are skipped); the newest is used unless `-s` says otherwise.

| Flag | Meaning |
|------|---------|
| `-s, --session SESSION` | Socket path, PID, or any substring of the project or level path (`-s arena`) |
| `-o, --output FORMAT` | `yaml`, `json`, `table`, `wide` (extra columns, e.g. GUIDs), `name` (`node/PATH` lines) |
| `--timeout SEC` | Reply timeout (default 10) |
| `-h, --help`, `--license`, `-v, --version` | |

Exit status: 0 success, 1 the editor reported an error (unknown node, failed save, validation errors), 2 usage error.

## Reading

| Command | Shows |
|---------|-------|
| `get nodes` | Flat table: PATH KIND VISIBLE LOCKED CHILDREN (+GUID with `-o wide`); `-o yaml/json` gives the nested tree |
| `get node PATH [PATH...]` | Full node(s) as YAML: guid, name, path, location, rotation (radians), scale, visible, locked, is_2d, visual {kind, primitive, asset, color, roughness, metallic, params}, components, scripts, children |
| `describe node PATH` | Same, recursive (children included) |
| `get selection` | Selected nodes (PRIMARY marked) |
| `get info` | pid, project, level, name, dirty, nodes, undo, redo, tool, space, snap..., selection, clients |
| `get project` | Manifest: root, name, default_level, game_output, run_command, scripts_dir, asset_dirs |
| `get actions` | Every editor action: ID LABEL SHORTCUT ENABLED (+CATEGORY CHECKED wide) |
| `get problems` | Validation: SEVERITY CODE PATH MESSAGE |
| `get assets [--type TYPE]` | Project files with their asset type |
| `get history` | Undo steps (done) and redo steps |
| `get view` | Viewport camera |
| `get state` | UI state: open menu/dialog, nav mode, rects, last input |
| `get play` | Play-in-editor state |
| `get methods` | Every IPC method name |
| `logs [--tail N] [-w]` | Editor log (console); `-w` follows |
| `events [TYPE...]` | Stream events as JSON lines (`-o yaml` for YAML docs): changed, selection, level, saved, tool, log, play, run |

Resource aliases: `node`/`no` = nodes, `sel` = selection, `action`, `problem`, `asset`, `editor`/`level` = info, `ui` = state, `session`.

## Changing nodes

Node names are slash paths from the top of the level (`Props/Crate`) or GUIDs. Values: `1,2,3` is a vector, `true`/`false`, numbers, `null`, JSON (`'{"a":1}'`), otherwise a string.

| Command | Effect |
|---------|--------|
| `create node NAME [--parent P] [--kind K] [--primitive SHAPE] [--index N] [key=value...]` | New node; trailing assignments set its transform/flags in the same undo step |
| `set node PATH key=value...` | `name`, `location`, `rotation_degrees`, `rotation` (radians), `scale`, `visible`, `locked`, `visual='{...}'` |
| `rename node PATH NEW_NAME` | |
| `move node PATH [--parent P] [--index N] [--keep-world]` | No `--parent` moves to the top level |
| `duplicate node PATH` | Copy with children next to the original |
| `delete node PATH...` | With children (undoable) |
| `edit node PATH` | Opens the editable fields as YAML in `$VISUAL`, else `$EDITOR`, else `vi` (arguments allowed, e.g. `emacsclient -t`); applies on save; no change or an empty file cancels |
| `apply -f FILE` / `apply -f -` | YAML (multi-document with `---`) or JSON (object or array). Each document needs `node:`; existing nodes are updated, missing ones are created (with missing parents, like `mkdir -p`) using `kind:`/`primitive:` then updated |
| `select PATH... [--add]`, `select --clear` | Editor selection |

Kinds: `none`, `primitive`, `mesh-asset`, `sprite`, `tilemap`, `light`, `camera`, `audio-emitter`. Primitives: `cube`, `uv-sphere`, `ico-sphere`, `plane`, `cylinder`, `cone`, `torus`, `circle`, `grid`, `rectangle-2d`, `circle-2d` (unit-sized, 1 m). Visual fields: `kind`, `primitive`, `asset` (project-relative path), `color` `[r,g,b,a]` 0..1, `roughness`, `metallic`, `params {}`; `color`/`roughness`/`metallic` may be nested under `material`.

Apply file example:

```yaml
node: Props/Crate
primitive: cube
location: [2, 0.5, -3]
rotation_degrees: [0, 45, 0]
scale: [1, 1, 1]
visual:
  material:
    color: [0.6, 0.4, 0.2, 1]
---
node: Props/Lantern
kind: light
location: [2, 2, -3]
```

Round trip: `lrgeditorctl get node Props/Crate -o yaml > crate.yaml`, edit, `lrgeditorctl apply -f crate.yaml` (the `guid` key is ignored; `node:` is required, add it or use `edit`).

## Editor operations

| Command | Effect |
|---------|--------|
| `run ACTION` | Any editor action (`run` alone lists them): e.g. `view.frame-all`, `view.top`, `edit.select-all`, `node.group`, `tool.rotate`, `view.toggle-2d`, `file.save` |
| `undo`, `redo` | |
| `save [PATH]` | Write the level (PATH = save as) |
| `reload` | Re-read the level from disk |
| `open LEVEL [--force]`, `new [NAME] [--force]` | `--force` discards unsaved changes |
| `validate` | Problems; exit 1 if any are errors |
| `play [start\|pause\|step\|stop\|state]`, `stop` | Play-in-editor; stop restores the level exactly |
| `game start\|stop` | Run the game as a process with the project's run command (output in `logs`) |
| `view top\|front\|right\|bottom\|back\|left\|persp\|all` | Camera presets; `view focus PATH`; `view set position=x,y,z target=x,y,z orthographic=true ortho_height=10` |
| `screenshot FILE.png [--window]` | Viewport (or whole window) as PNG; needs a windowed editor |
| `tiles NODE [paint\|rect\|fill\|pick\|configure key=value...]` | Print a tilemap grid or paint it: `tiles Ground configure width=16 height=8`, `tiles Ground paint tile=3 'points=[[0,0],[7,0]]' brush=2`, `tiles Ground fill x=0 y=0 tile=1` |
| `timeline [list\|get NAME\|add NAME\|rm NAME\|key NAME NODE key=value...\|play [NAME]\|stop\|seek TIME [NAME]\|state]` | Keyframes: `timeline key Door Props/Door time=1 channel=rotation.y value=1.57 easing=ease-in-out-sine` |
| `thumbnail PATH` | Generate (if needed) and print an asset's cached thumbnail PNG |
| `notify MESSAGE [--level LEVEL]` | Toast in the editor; LEVEL is `info` (default), `success`, `warning` or `error` (`0`-`3` also work; anything else exits 2) |
| `meta get PATH`, `meta set PATH key=value... [guid=G]` | An asset's `.meta` sidecar (`asset.meta.*`): `meta set assets/crate.png filter=point max-size=512`; all or nothing, `null` resets a setting |
| `vcs [PATH]` (alias `git`) | Git branch and files that are not clean (`-o wide` adds the badge letter); with PATH, that file's or folder's status |
| `prefab overrides\|apply\|revert\|relink\|unlink NODE [keys=a,b] [path=P]`, `prefab sync [PATH]` | Linked prefab instances: `prefab apply Props/Crate keys=transform/scale` writes the override into the prefab and syncs the other instances |
| `view layout single\|split-h\|split-v\|quad`, `view render-mode MODE [viewport=N]`, `view overlay NAME on\|off [viewport=N]` | Viewport layout, render mode (lit, unlit, wireframe, normals, lighting-only) and overlays; without an argument they print the current state |
| `inspector list\|get\|set\|reset\|state\|apply-override\|revert-override [NODE] [NAME] [key=value...]` | The inspector: `inspector set Props/Crate scale section=transform value=2,2,2`; several nodes via `nodes='["A","B"]'` is one undo step |
| `hierarchy reveal\|rename NODE [NEW_NAME]`, `hierarchy filter TEXT`, `hierarchy state` | The hierarchy panel |
| `asset reveal\|duplicate\|trash PATH`, `asset rename PATH NAME`, `asset move PATH DEST`, `asset orphans [trash=true]` | Asset files with their `.meta` (GUIDs kept on rename and move) |
| `code open PATH[:LINE]`, `code list`, `code save [PATH]` | The built-in code editor panel |
| `profiler [frames=N] [enable=true\|false]` | Profiler sections as a table |
| `data list`, `data open PATH` | Data editors (input maps, locales, sound banks, themes, HUD/UI layouts, dialog trees, dungeons); `list` is a FORMAT/DIRTY/ISSUES/ERRORS/PATH table |
| `data get [PATH] [AT]`, `data set [PATH] AT VALUE`, `data edit [PATH] OP AT [key=value...]` | Read and change a document by YAML path (`/actions/jump/bindings/0/key`); PATH is optional (default: the focused data editor) and is recognised as the file when it does not start with `/` or is an existing file. `data set /title Gate`; `data edit add_key /actions key=fire 'yaml={bindings: []}'`; ops: insert (index, yaml), move (from, to), add_key (key, yaml, position), rename_key (old, new), remove |
| `data undo\|redo\|validate\|save\|reload\|close\|reveal [PATH] [AT] [key=value...]` | Per-file history; `validate` exits 1 on errors; `save save_as=FILE`; `close --force` discards unsaved edits; `reveal AT` shows the node like clicking an issue. All of them work against `lrgeditor --headless` |
| `palette [commands\|files] [QUERY]`, `preferences [PAGE]` | Open the command palette / quick-open, or the preferences dialog (e.g. `preferences keybindings`) for the human |
| `call METHOD [key=value...]` or `call METHOD '{json}'` | Any IPC method (see `get methods`) |
| `quit [--force]` | Close the editor |
| `mcp` | Serve MCP on stdio (see mcp.md) |
| `completion bash` | `source <(lrgeditorctl completion bash)`; installed to `share/bash-completion/completions` |

## Scripting patterns

```bash
# Every light, by path
lrgeditorctl get nodes -o json | yq -p json '.. | select(.kind? == "light") | .name'

# Raise everything under Props by 1 m (GUIDs stay valid while paths change)
for guid in $(lrgeditorctl get nodes -o wide | awk '$1 ~ /^Props\// { print $NF }')
do
	loc="$(lrgeditorctl get node "${guid}" -o json | yq -p json -o csv '.location')"
	IFS=, read -r x y z <<<"${loc}"
	lrgeditorctl set node "${guid}" "location=${x},$(awk "BEGIN{print ${y}+1}"),${z}"
done

# CI gate on a level, no window needed
lrgeditor --validate levels/arena.rlevel

# React to saves
lrgeditorctl events saved | while read -r line; do echo "saved: ${line}"; done
```

## 0.9.0 rendering verbs
Positionals fill named params in order; file args are sent absolute when they exist in cwd, else resolved against the project; VALUE/INDEX/FROM/TO/LEVEL/ENABLED parse like assignment values (JSON, numbers, true/false, null, a,b,c vectors, on/off); extra key=value pass through; output YAML (-o json). Bad sub-command: usage, exit 2.
- material open|create|get|save PATH; material create PATH [parent=P] [shading_model=lit|unlit|custom] [shader=S]; material variant PATH [DEST]; material set PATH KEY VALUE; material revert PATH KEY; material preview PATH [sphere|cube|plane] [rotate=BOOL]; material assign NODE PATH; material list (alias mat)
- shader create PATH [lit=BOOL] | shader compile|diagnostics|uniforms|reload PATH
- shadergraph open|create|get|validate|glsl|save PATH; shadergraph types; shadergraph add PATH TYPE [id= x= y=]; remove PATH ID; connect PATH FROM FROM_PORT TO TO_PORT; disconnect PATH TO TO_PORT; param PATH ID KEY VALUE; input PATH ID PORT VALUE|null (alias sg)
- env [get] | env set KEY VALUE | env reset | env bake [--wait] | env progress | env show
- post types | post open|get|save|assign PATH | post create PATH [preset=default|empty] | post add PATH TYPE [index=N] | post remove PATH INDEX | post move PATH FROM TO | post set PATH INDEX KEY VALUE | post preview [PATH] on|off | post look PATH [viewport=N]
- lod get NODE | lod force NODE LEVEL|auto
- view layers [MASK|all|none|0xHEX] | view layers LAYER on|off [viewport=N]; view render-mode|mode MODE (10 modes)
Examples: lrgeditorctl material set materials/steel.rmat metallic 1; lrgeditorctl env set fog.density 0.02 && lrgeditorctl env bake --wait; lrgeditorctl post add looks/night.rpost vignette; lrgeditorctl view mode light-complexity viewport=1
