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
| `notify MESSAGE [--level 0-3]` | Toast in the editor |
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
