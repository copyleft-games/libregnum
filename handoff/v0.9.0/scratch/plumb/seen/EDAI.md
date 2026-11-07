# FINAL
# EDAI IPC methods (implemented and tested: tests/test-editor-app-ai.c)

Conventions: `path` = the .rbt / .rfsm file (absolute or project-relative); the method opens its editor tab when needed (like anim.*). Errors (reply error.code nick): invalid-params (missing/wrong param, refused edit such as a second child for a decorator), not-found (no such file/node/state/entity/brain), failed (engine/document error, file exists on create, nothing live). Every edit is one undo step of the document.
JSON actions (fsm): {"action":NAME,"params":{}} | {"script":NAME,"params":{}} | {"set":KEY,"value":V} | {"event":NAME}
JSON conditions (fsm): {"key":KEY,"op":"eq|ne|lt|le|gt|ge|set|unset","value":V} | {"condition":NAME,"params":{}} | {"script":NAME,"params":{}}

## bt.* (src/editor/ui/data/lrg-editor-bt-editor.c)
- bt.open {path} -> {path}
- bt.create {path, name?} -> {path} ; writes a starter tree (root selector) and opens it; FAILED if the file exists
- bt.get {path} -> {path, name, shown ("" = main root, else inline subtree name), root (id|null), nodes:[{id, explicit, type, kind (composite|decorator|leaf|subtree), name|null, parent (id|null), index, yaml_path, props{}, params{}, x, y}], blackboard:[{name, type, default?}], subtrees:[{name, file|null}], selected (id|null), dirty, load_error?}
- bt.palette {} -> {types:[{name, kind}], actions:[names], conditions:[names]}
- bt.add_node {path, type, parent? (id; omitted = new root when the tree is empty), index? (-1 append), id?, name?, props?{}, params?{}, x?, y?} -> {id}
- bt.remove_node {path, id} -> {removed} ; removes the subtree under it
- bt.move_node {path, id, parent, index?} -> {id} ; re-parent / reorder
- bt.set_node {path, id, new_id?, name? (null clears), type?, props?{key: value|null}, params?{key: value|null}} -> {id}
- bt.set_position {path, id, x, y} -> {id}
- bt.layout {path} -> {nodes} ; auto layout, positions saved in `editor: positions:`
- bt.select {path, id|null} -> {selected}
- bt.show {path, subtree|null} -> {shown} ; show an inline subtree's root (null: the main root)
- bt.add_key {path, name, type (int|float|bool|string|object), default?} -> {name}
- bt.remove_key {path, name} -> {removed}
- bt.set_subtree {path, name, file?} -> {name} ; file: a file subtree, else an inline one (empty selector root)
- bt.remove_subtree {path, name} -> {removed}
- bt.open_subtree {path, id} -> {path} (file subtree: the opened file) | {shown} (inline)
- bt.validate {path} -> {ok, issues:[{severity, path, line, message, node (id|null)}]}
- bt.save {path} -> {path}
- bt.live {path} -> {playing, attached, entity|null, status, tick, current|null, active_path:[ids], nodes:[{id, status, visited, ticks}]}

## fsm.* (src/editor/ui/data/lrg-editor-fsm-editor.c)
State paths are "a/b" ("" = the root, i.e. the top level). `from` "" = any-state transitions (the root's).
- fsm.open {path} -> {path}
- fsm.create {path, name?} -> {path} ; starter machine (idle state); FAILED if the file exists
- fsm.get {path, machine?} -> {path, name, machine (shown state path), breadcrumbs:[names], initial, blackboard:[{name,type,default}], states:[{name, path, initial(bool), children, enter:[actions], update:[actions], exit:[actions], x, y}], enter/update/exit:[actions] (machine-wide), transitions:[{from, index, to, event|null, after|null, when:[conds], do:[actions], fires}] (every transition of the machine, root's first, depth first), selected:{state|null, from|null, index}, dirty, load_error?}
- fsm.enter {path, state} -> {machine} ; show a state's sub-machine ("" = top)
- fsm.up {path} -> {machine}
- fsm.add_state {path, name, parent? ("" top), x?, y?} -> {state}
- fsm.remove_state {path, state} -> {removed} ; also removes transitions targeting it
- fsm.rename_state {path, state, name} -> {state} ; rewrites `to:`/`initial:` naming it
- fsm.set_initial {path, state} -> {state}
- fsm.add_transition {path, from, to, event?, after?, when?:[conds], do?:[actions]} -> {from, index}
- fsm.set_transition {path, from, index, to?, event?|null, after?|null, when?, do?} -> {from, index}
- fsm.remove_transition {path, from, index} -> {removed}
- fsm.set_actions {path, state, phase (enter|update|exit), actions:[actions]} -> {count}
- fsm.set_position {path, state, x, y} -> {state}
- fsm.select {path, state?} | {path, from, index} -> {state|null, from?, index?}
- fsm.add_key {path, name, type, default?} -> {name} ; fsm.remove_key {path, name} -> {removed}
- fsm.validate {path} -> {ok, issues:[{severity, path, line, message}]}
- fsm.save {path} -> {path}
- fsm.live {path} -> {playing, attached, entity|null, running, active:[{state, time}], last_transition:{from,to,event}|null, queued:[] (always empty: the engine does not expose the queue; see ai.state debug), fires:[{from, index, to, count}]}
- fsm.send_event {path, event} -> {sent} ; queues on the attached live machine (FAILED when not playing / not attached)

## ai.* (src/editor/ui/lrg-editor-ai-debug-panel.c, the docked "AI" panel)
`entity` = the node GUID of a live entity made from a level node, else its name.
- ai.show {} -> {shown}
- ai.entities {} -> {playing, selected|null, entities:[{entity, name, tree|null, machine|null, current_node|null, current_state|null, running, keys, focused}]}
- ai.select {entity|null} -> {selected} ; a name is resolved to its id while playing (not-found if no live entity matches); null follows the selection again
- ai.state {entity?} -> {entity, debug (YAML text of lrg_ai_component_dup_debug_state), blackboard:[{key, type, value}]}
- ai.set_key {entity?, key, value, type? (int|float|bool|string; default: the key's current type, else from the JSON value)} -> {key, value}
- ai.send_event {entity?, event} -> {sent}
- ai.brains {} -> {brains:[{name, provider, model|null, asked, answered, cached, failed, input_tokens, output_tokens, mean_seconds, topics:[{topic, busy, pending_seconds, latency, asked, answered, cached, failed, error|null}]}]}
- ai.topic {brain, topic} -> {brain, topic, state (YAML text), request (JSON text|null)}
- ai.log {brain?, limit? (default 50)} -> {entries:[{time, brain, kind (asked|answered|failed), topic, text}]}
(brains are registered by the embedding game/tool in C: lrg_editor_ai_debug_panel_watch_brain (app, name, brain))

Actions (lrg_editor_actions): ai.show (View > AI Panel).
CLI suggestions: lrgeditorctl bt get|add|rm|mv|set|layout|validate|save|live PATH ...; fsm get|add-state|rm-state|rename|add-transition|set-transition|rm-transition|set-actions|validate|save|live|event PATH ...; ai entities|select|state|set|event|brains|topic|log.
