# libregnum editor MCP server

The editor exposes its operations as MCP (Model Context Protocol) tools, implemented with mcp-glib. The same tool set is served two ways:

| Command | When |
|---------|------|
| `lrgeditorctl mcp [-s SESSION]` | Bridge to an editor that is already running (a person's windowed session, or a headless one). Each call finds the session again, so it survives editor restarts and can be configured before any editor runs. |
| `lrgeditor --mcp [PROJECT]` | The agent starts its own editor. MCP runs inside the editor process; it is headless when there is no display. A headless editor exits when the client disconnects, unless the level has unsaved changes (then it keeps running for `lrgeditorctl save`/`quit`). The normal IPC socket stays available too, so `lrgeditorctl` works alongside. |

## Registering with clients

```bash
# Claude Code
claude mcp add libregnum -- lrgeditorctl mcp
claude mcp add libregnum-keep -- lrgeditor --mcp /home/me/games/keep

# Any client with an mcpServers JSON config
{
  "mcpServers": {
    "libregnum": { "command": "lrgeditorctl", "args": ["mcp"] }
  }
}
```

Use absolute paths for project directories in configs. With several editors running, pin one: `lrgeditorctl mcp -s keep`.

## Tools

| Tool | IPC method | Notes |
|------|-----------|-------|
| `editor_info` | editor.info | Call first: level, dirty, counts, tool, selection |
| `list_sessions` | (socket scan) | Running editors |
| `get_tree` | node.tree | `{node?}` |
| `get_node` | node.get | `{node, recursive?}` |
| `find_nodes` | node.find | `{query}` (name substring or GUID) |
| `create_node` | node.create (+ node.set) | `{name, parent?, kind?, primitive?, index?, location?, rotation_degrees?, scale?}` in one undo step |
| `update_node` | node.set | `{node, name?, location?, rotation_degrees?, rotation?, scale?, visible?, locked?, visual?}` |
| `delete_node` | node.delete | destructive (undoable) |
| `duplicate_node` | node.duplicate | |
| `move_node` | node.move | `{node, parent?, index?, keep_world?}` |
| `add_component` / `set_component` / `remove_component` | component.* | registered component type names |
| `add_script` / `remove_script` | script.* | `{node, script, language?}` |
| `get_selection` / `set_selection` | selection.* | |
| `undo` / `redo` / `get_history` | edit.* | |
| `save_level` | level.save | `{path?}` |
| `open_level` / `new_level` | level.open / level.new | destructive with `force` |
| `get_level_yaml` | level.serialize | the whole `.rlevel` as text |
| `validate_level` | level.validate | `[]` means clean |
| `list_actions` / `run_action` | action.* | any menu command by id |
| `play` | play.* | `{command: start\|pause\|step\|stop\|state}` |
| `run_game` | run.start / run.stop | `{stop?}` |
| `get_view` / `set_view` / `focus_view` | view.* | camera |
| `screenshot` | view.screenshot | returns a PNG image; `{window?}`; windowed editors only |
| `get_log` | log.tail | `{count?}` |
| `list_assets` | asset.list | `{type?}` |
| `project_info` | project.info | |
| `ui_state` | ui.state | |
| `notify` | ui.notify | toast for the human; `{message, level?: info\|success\|warning\|error}` |
| `get_tilemap` / `configure_tilemap` | tilemap.get / tilemap.configure | grid of a `tilemap` node; width/height, tile px, tile_size, tileset, layers |
| `paint_tiles` / `paint_tile_rect` / `fill_tiles` | tilemap.paint / rect / fill | `cells` or brush `points`; tile ids 1-based, 0 erases; one undo step each |
| `list_timelines` / `get_timeline` | timeline.list / get | keyframe clips stored in the level |
| `set_keyframe` / `remove_keyframe` / `configure_timeline` | timeline.key / unkey / set | channel + value, or the node's transform; easing nicks |
| `preview_timeline` | timeline.seek | pose the level at a time (not saved) |
| `get_thumbnail` | asset.thumbnail | cached PNG path of a texture/model/level/prefab |
| `get_asset_meta` / `set_asset_meta` | asset.meta.get / set | `.meta` sidecar: guid, importer, settings; set is all or nothing |
| `vcs_status` | vcs.status | git branch and files that are not clean; `{path?}` |
| `prefab_overrides` | prefab.overrides | read-only: source, linked, override keys |
| `prefab_apply` / `prefab_revert` | prefab.apply / revert | destructive: `{node, keys?}`; apply overwrites the prefab file and syncs other instances |
| `prefab_sync` / `prefab_relink` / `prefab_unlink` | prefab.sync / relink / unlink | unlink is destructive (undoable) |
| `set_viewport_layout` / `set_render_mode` / `set_overlay` | view.layout / render_mode / overlay | without the value they report the current state |
| `inspector_list` / `inspector_get` | inspector.list / get | read-only: sections and rows `{name, label, group, hint, value, mixed, state}` |
| `inspector_set` / `inspector_reset` | inspector.set / reset | `{node\|nodes, section, component?, name, value}`; several nodes = one undo step |
| `inspector_apply_override` / `inspector_revert_override` | inspector.apply_override / revert_override | destructive: one prefab override row |
| `inspector_state` | inspector.state | read-only, no parameters: lock, shown nodes, live object, prefab (lock it with `run_action` `inspector.toggle-lock`) |
| `hierarchy_reveal` / `hierarchy_rename` / `hierarchy_state` | hierarchy.* | reveal and select, inline or direct rename, visible rows |
| `hierarchy_filter` | hierarchy.filter | `{text?}`: filter the panel (empty clears); returns the match count |
| `asset_reveal` / `asset_rename` / `asset_move` / `asset_duplicate` | asset.* | the `.meta` follows its asset; duplicates get a new guid |
| `asset_trash` / `asset_orphans` | asset.trash / orphans | destructive: to the desktop trash |
| `profiler_sample` | profiler.sample | `{frames?, enable?}` |
| `code_open` / `code_list` / `code_save` | code.* | the built-in code editor panel |
| `open_palette` / `open_preferences` | palette.open / preferences.open | `{mode?: commands\|files, query?}` / `{page?}`: open them for the human |
| `data_open` / `data_list` | data.open / data.list | open a data file (input map, locale, sound bank, theme, HUD/UI layout, dialog tree, dungeon) in its editor; list is read-only |
| `data_get` / `data_validate` | data.get / data.validate | read-only: `{path?, at?, as?}` node `{at, kind, value, keys, length, yaml, line}`; `{ok, errors, issues}` |
| `data_set` / `data_edit` | data.set / data.edit | `{path?, at, value\|yaml, gesture?}`; `{path?, op, at, ...}` with op insert, move, add_key, rename_key, remove; one undo step each |
| `data_undo` / `data_redo` / `data_save` / `data_reveal` | data.* | per-file history; `save_as`; reveal shows a YAML path or line |
| `data_reload` / `data_close` | data.reload / data.close | destructive: reload and `close {force: true}` discard unsaved edits |
| `call_method` | any | `{method, params}` escape hatch for any other IPC method |

Results are JSON text (YAML text for `get_level_yaml`); failures come back as tool errors with the editor's message, e.g. `node.get: No node 'Hall/Tabel' (not-found)`.

## Working well with it

- Start with `editor_info` and `get_tree`; address nodes by path, switch to GUIDs when renaming/moving many.
- Size primitives with `scale` (they are 1 m); `rotation_degrees` is easier to reason about than radians.
- After a batch of edits: `validate_level`, then `screenshot` if the editor is windowed, then report and ask before `save_level` unless the user asked to save.
- `play` start/stop is safe: stop restores the level and keeps edits made during play on the redo stack.
- Never pass `force` to `open_level`/`new_level` without the user's explicit OK.
