# VSCRIPT IPC methods (DRAFT, names and params stable; FINAL marker comes when done)

Docs: docs/editor/visual-scripting.org (#vscript-ipc).

Registered by src/editor/ui/data/lrg-editor-vscript-editor.c (vscript.*). Common: `path` = a
`.rvscript` file, absolute or relative to the project (default: the focused visual script
editor, graph or blocks); the tab is opened when needed. `graph` = a subgraph name (default/""
= the main graph). Node pins are addressed "node.pin" (e.g. "branch.true", "add.a").
Every mutating method is ONE undo step in the document's history (`data.undo` reverts it).
Errors: invalid-params (missing/bad params, a bad pin, an incompatible link), not-found (no such
file/editor/node/variable/subgraph/comment), failed (engine errors: load, save, runtime).

## vscript.*
- vscript.create {path, name?, view?:"graph"|"blocks"} -> {path, view}   (writes a starter program: an `event.start` node wired to a `debug.log`; refuses to overwrite)
- vscript.open {path, view?:"graph"|"blocks"} -> {path, panel, view}   (view switches the program's `view:` key and reopens it in that editor)
- vscript.get {path?, graph?} -> {path, dirty, view, name, description, seed, step_budget, fixed_step, variables:[{name, type, default}], subgraphs:[name], graph, interface:{inputs:[{name,type}], outputs:[{name,type}]}, nodes:[{id, type, title, x, y, params:{}, inputs:{}, pins:[{name, direction:"input"|"output", exec:bool, type}]}], links:[{from, to}], comments:[{id, text, x, y, width, height, color}], selected:[node ids], issues:[{severity, graph, node, path, message}], error?}
- vscript.node_types {query?, category?} -> {types:[{type, title, category, description, event:bool, pure:bool}]}   (read-only; the palette)
- vscript.gi_search {query, limit?=50} -> {available:bool, results:["Namespace.Type.method" | "Namespace.function"]}   (read-only; GIRepository search over the loaded typelibs: GLib, GObject, Gio, Libregnum, Graylib)
- vscript.add_node {path?, graph?, type, id?, x?, y?, params?:{k:v}, inputs?:{pin:value}} -> {id, pins}
- vscript.remove_node {path?, graph?, id} -> {removed:true}   (drops its links)
- vscript.set_node {path?, graph?, id, x?, y?, params?:{k:v|null}, inputs?:{pin:v|null}, rename?} -> {id, pins}
- vscript.connect {path?, graph?, from:"node.pin", to:"node.pin"} -> {from, to}   (replaces the link into a data input / out of an exec output; type checked)
- vscript.disconnect {path?, graph?, from, to} -> {removed:bool}
- vscript.set_variable {path?, name, type?:"bool"|"int"|"float"|"string"|"object"|"any", default?, remove?:bool} -> {name, type, default} | {removed:true}
- vscript.add_subgraph {path?, name, inputs?:[{name,type}], outputs?:[{name,type}]} -> {name}
- vscript.remove_subgraph {path?, name} -> {removed:true}
- vscript.add_comment {path?, graph?, text, x, y, width?, height?, color?} -> {id}   (a comment frame groups the nodes inside it when dragged)
- vscript.remove_comment {path?, graph?, id} -> {removed:true}
- vscript.set_program {path?, name?, description?, seed?, step_budget?, fixed_step?} -> vscript.get
- vscript.select {path?, nodes:[id]} -> {selected:[id]}   (not an edit)
- vscript.validate {path?} -> {ok, issues:[{severity, graph, node, path, message}]}   (read-only)
- vscript.run {path?, frames?=1, dt?=1/60, event?:custom event name, args?:[v], variables?:{name:v}} -> {ok, steps, time, log:[text], variables:{name:v}, error?}   (read-only: runs the UNSAVED program headless in a sandbox: start, then `frames` updates, then the custom event when given)
- vscript.live {path?} -> {playing, instances, active:[node ids executed in the last 10 frames], hits:{node:count}, paused_at?:node}   (read-only: the play session's running LrgScriptingGraph instances of this file)
- vscript.breakpoint {path?, graph?, node, enabled:bool} -> {node, enabled}   (node-level breakpoint in every running instance of the file; also stored for instances started later)
- vscript.continue {path?} -> {resumed}   (resumes instances paused on a breakpoint)
- vscript.save {path?} -> {path}
