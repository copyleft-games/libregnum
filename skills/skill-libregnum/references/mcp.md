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
| `notify` | ui.notify | toast for the human |
| `call_method` | any | `{method, params}` escape hatch |

Results are JSON text (YAML text for `get_level_yaml`); failures come back as tool errors with the editor's message, e.g. `node.get: No node 'Hall/Tabel' (not-found)`.

## Working well with it

- Start with `editor_info` and `get_tree`; address nodes by path, switch to GUIDs when renaming/moving many.
- Size primitives with `scale` (they are 1 m); `rotation_degrees` is easier to reason about than radians.
- After a batch of edits: `validate_level`, then `screenshot` if the editor is windowed, then report and ask before `save_level` unless the user asked to save.
- `play` start/stop is safe: stop restores the level and keeps edits made during play on the redo stack.
- Never pass `force` to `open_level`/`new_level` without the user's explicit OK.
