# Editor IPC protocol (summary)

Full reference: `docs/editor/ipc-protocol.org` in the libregnum checkout. `lrgeditorctl` and the MCP tools are thin layers over this protocol; use it directly from programs that keep a connection open (Emacs does, via `editors/emacs/libregnum.el`).

## Transport and messages

- Unix stream socket `$XDG_RUNTIME_DIR/libregnum/editor-PID.sock` (mode 0600), or `--socket` / `$LRG_EDITOR_SOCKET`. Paths must fit 107 bytes.
- One JSON object per line:

```text
request   {"id": 7, "method": "node.get", "params": {"node": "House/Door"}}
result    {"id": 7, "result": {...}}
error     {"id": 7, "error": {"code": "not-found", "message": "No node 'House/Door'"}}
event     {"event": "selection", "data": {"nodes": ["8c1e..."], "primary": "8c1e..."}}
```

- A request without `id` is a notification (no reply). Error codes: `parse`, `invalid-request`, `method-not-found`, `invalid-params`, `not-found`, `failed`.
- `hello {client, capabilities: ["open-file"], subscribe: ["changed", ...|"*"]}` declares a client; without `subscribe` a client gets every event except `log`. A client with `open-file` receives `open-file {file, line, col}` events instead of the editor launching `$EDITOR`.

## Methods

Document: `hello`, `ping`, `methods`, `editor.info`, `level.new|open|save|reload|serialize`, `node.tree|list|get|find|set|create|delete|duplicate|move|rename`, `component.add|remove|set`, `script.add|remove|open`, `file.open`, `selection.get|set|clear`, `edit.undo|redo|history|jump|copy|cut|paste`, `tool.set`, `prefab.instantiate|save`, `log.tail`.

Tools (headless): `tilemap.get|configure|paint|rect|fill|pick` (tilemap nodes: `cells [[x,y]...]` or a brush stroke through `points`, `tile` 1-based into the tileset, 0/`erase` erases, `layer` index or name), `timeline.list|get|add|remove|set|key|unkey|move-key|ease|sample` (channels `location.x|y|z`, `rotation.x|y|z` radians, `scale.x|y|z`, `color.r|g|b|a`, `roughness`, `metallic`, `param.NAME`; `easing` = LrgEasingType nick). Every edit is one undo step.

Application: `action.list|run`, `view.get|set|focus|screenshot`, `play.start|stop|pause|step|state`, `run.start|stop`, `project.info|open`, `asset.list`, `asset.thumbnail {path, wait}`, `level.validate|import`, `ui.notify|state`, `tilemap.tool`, `timeline.play|stop|seek|state` (preview; never saved), `editor.quit`.

## Events

`changed {dirty, undo, redo, nodes, last}`, `selection {nodes, primary}`, `level {path, name}`, `saved {path}`, `tool {tool, space, snap}`, `log {serial, time, level, domain, message}`, `play`, `run`, `open-file {file, line, col}`.

## From bash

```bash
sock="$(ls -t "${XDG_RUNTIME_DIR}"/libregnum/editor-*.sock | head -n 1)"
printf '{"id":1,"method":"node.find","params":{"query":"Door"}}\n' \
	| socat -t 2 - UNIX-CONNECT:"${sock}" | yq -p json '.result'
```
