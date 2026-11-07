# spec.py: the M6/M7 IPC methods (from m67e-ipc/*.md) for gen.py.
#
# F(family...) starts a family; m(method, cli, params, result, desc, ro=, de=, verb=)
#   cli:    positional keys for lrgeditorctl ('@' path, '~' typed value)
#   params: "name:type[!]" ...; types s i n b o a sa na v (any); '!' required
#   result: the docs' Result column (org markup added by gen.py)
#   desc:   the MCP tool description
#   ro/de:  read-only / destructive hints; verb: CLI verb override

FAMILIES = []


class Family:
    def __init__(self, name, cli, reg, sec, title, intro, cli_intro, examples, smoke_name=None,
                 wave='M6/M7', hybrid=None):
        # wave: the 0.9.0 milestone (comments, docs); hybrid: verbs a
        # hand-written cmd_FAMILY() keeps (open/list/save of "code"), listed
        # first in the completion and usage; other verbs fall through to
        # the generated table.
        self.wave = wave
        self.hybrid = hybrid or []
        self.name = name
        self.cli = cli
        self.reg = reg
        self.sec = sec
        self.title = title
        self.intro = intro
        self.cli_intro = cli_intro
        self.examples = examples
        self.methods = []


def F(*a, **k):
    f = Family(*a, **k)
    FAMILIES.append(f)
    return f


def m(method, cli, params, result, desc, ro=False, de=False, verb=None, tool=None):
    FAMILIES[-1].methods.append(dict(method=method, cli=cli, params=params, result=result,
                                     desc=desc, ro=ro, de=de, verb=verb, tool=tool))


# ===========================================================================
# EDNAV: nav.*
# ===========================================================================
F('nav', 'nav', 'Navigation panel', 'ipc-nav', 'Navigation methods',
  'Registered by the Navigation panel (=src/editor/ui/lrg-editor-nav-panel.c=). Everything works on the open level (=failed= "No level is open" without one). The panel state is the level\'s =navigation:= block plus the in-memory =.rnav= bake settings (=LrgNavBake=); =dirty= says the settings differ from the =.rnav= file on disk. Every mutating method except =nav.bake=, =nav.cancel=, =nav.overlay=, =nav.select= and =nav.debug_agent= is one undo step of the level (=edit.undo= reverts it). Vectors are =[x, y, z]=; =area= is an index or a name. Errors: missing or bad parameters =invalid-params=, an unknown area, box, link or file =not-found=, an engine error (bake, load, save) =failed=.',
  '=nav= drives the Navigation panel on the open level. =set= takes =key=value= pairs (sent as =values=); the area, box and link verbs take their indices positionally.',
  ['lrgeditorctl nav set agent_radius=0.4 cell_size=0.2', 'lrgeditorctl nav area-add water cost=4; lrgeditorctl nav area-box-add water min=-5,0,-5 max=5,1,5',
   'lrgeditorctl nav link-add start=0,0,0 end=0,2,4 bidirectional=false', 'lrgeditorctl nav bake; lrgeditorctl nav save',
   'lrgeditorctl nav overlay on=polygons,links; lrgeditorctl nav agents'])
m('nav.get', '', '', '{settings, settings_path, data, bake_on_load, dirty, bake: {name, agent: {radius, height, max_slope, max_step, max_drop}, voxel: {cell_size, cell_height}, regions: {min_area, corner_margin}, sources: {meshes, colliders, static_only, layer_mask, include_tags, exclude_tags, physics_layers, bounds}, output}, areas: [{index, name, tag, cost, color, boxes: [{min, max}]}], links: [{index, start, end, bidirectional, cost, area}], mesh: {present, unsaved, polygons, regions, links, boundaries, triangles, seconds}, baking, playing, overlay, selection: {area, box, link}}',
  'The level\'s navigation: the navigation: block refs (settings .rnav, baked data, bake_on_load), the LrgNavBake settings (agent, voxel, regions, sources, output), area types with their boxes, off-mesh links, the baked mesh summary, overlay flags and the selection.', ro=True)
m('nav.set', '', 'key:s value:v values:o', 'as =nav.get=; one undo step',
  'Change bake settings or the navigation: block in one undo step: {key, value} or {values: {key: value}}. Keys: name, agent_radius, agent_height, max_slope, max_step, max_drop, cell_size, cell_height, min_region_area, corner_margin, include_meshes, include_colliders, static_only, layer_mask, include_tags, exclude_tags, physics_layers, bounds ({min,max} or null), output, settings (an existing .rnav to load), data, bake_on_load. - and _ are the same. Returns nav_get.')
m('nav.new', '@path', 'path:s', 'as =nav.get=; the =settings:= ref change is undoable, the file stays',
  'Write a default .rnav (default <level-stem>.rnav next to the level) and make it the level\'s navigation settings. Returns nav_get.')
m('nav.bake', '', 'async:b save:b', '={polygons, regions, links, boundaries, triangles, seconds, saved}=; with =async= ={started: true}=',
  'Bake the level\'s navigation mesh with the current settings (save defaults to true when the level has a file: writes the .rnavmesh and sets data:). async: start in the background and return {started}.')
m('nav.cancel', '', '', '={cancelled}= (an async bake\'s result is dropped)', 'Cancel a running async bake.')
m('nav.save', '', '', 'as =nav.get=', 'Write the .rnav settings and the baked .rnavmesh (output, else <level-stem>.rnavmesh) and set the settings/data refs in one undo step. Returns nav_get.')
m('nav.clear', '', '', '={cleared}=; undoable', 'Drop the baked mesh and the data: ref (undoable).', de=True)
m('nav.area.add', 'name', 'name:s! tag:i cost:n', '={index}=', 'Add an area type (name, tag (auto when omitted), cost multiplier, default 1). Returns {index}.')
m('nav.area.remove', '~area', 'area:v!', '={removed}=; links naming it lose their area', 'Remove an area type (index or name) and its boxes; links naming it lose their area.', de=True)
m('nav.area.set', '~area', 'area:v! name:s tag:i cost:n', '={index}=', 'Change an area type: name, tag, cost.')
m('nav.area.box.add', '~area', 'area:v! min:na! max:na!', '={index}=', 'Add a box volume [min, max] (world [x,y,z]) that marks polygons inside it with the area.')
m('nav.area.box.remove', '~area ~box', 'area:v! box:i!', '={removed}=', 'Remove box index box of an area.', de=True)
m('nav.area.box.set', '~area ~box', 'area:v! box:i! min:na max:na', '={index}=', 'Move or resize box index box of an area: min, max [x,y,z].')
m('nav.link.add', '', 'start:na! end:na! bidirectional:b cost:n area:s', '={index}=', 'Add an off-mesh link from start to end ([x,y,z]); bidirectional (default true), cost (default 0), area name.')
m('nav.link.remove', '~index', 'index:i!', '={removed}=', 'Remove the off-mesh link at index.', de=True)
m('nav.link.set', '~index', 'index:i! start:na end:na bidirectional:b cost:n area:v', '={index}=', 'Change the off-mesh link at index: start, end, bidirectional, cost, area (a name, or null to clear).')
m('nav.select', '', 'area:v box:i link:i', '={area, box, link}= (-1 = none)', 'Select an area, area box or link for the viewport handles (-1 clears). Not undoable.')
m('nav.overlay', '', 'flags:sa on:sa off:sa', '={flags}=', 'Navigation overlay flags in the viewport: flags replaces, on/off toggle. Nicks: polygons, polygon-edges, boundaries, portals, links, areas, agents, paths, velocities, orca, neighbours, handles, all, default. No arguments reads them.')
m('nav.agents', '', '', '={playing, step_seconds, step_count, debug, agents: [{index, guid, node, entity, state, position, velocity, speed, remaining, destination, path, corner}]}= (empty when not playing)',
  'While playing: the play world\'s crowd agents (state idle/moving/on-link/arrived/failed, position, velocity, remaining distance, destination, path corners), the crowd step time and the debug agent.', ro=True)
m('nav.debug_agent', '', 'index:i guid:s node:s clear:b', '={debug}=', 'Pick the crowd\'s debug agent (its path and ORCA are drawn) by index, node guid or node path; clear: true clears.')
m('nav.show', '', '', '={panel, shown}=', 'Show and raise the Navigation panel.')

# ===========================================================================
# EDAI: bt.*, fsm.*, ai.*
# ===========================================================================
F('bt', 'bt', 'behaviour tree editor', 'ipc-bt', 'Behaviour tree methods',
  'Registered by the behaviour tree editor (=src/editor/ui/data/lrg-editor-bt-editor.c=). =path= is the =.rbt= file, absolute or project relative; the method opens its editor tab when needed. Node ids are the =id:= of =LrgBtAssetNode= (the asset generates =n0=, =n1=, ... for nodes without one). Every edit is one undo step of the document (=data.undo= undoes it). Errors: a missing or wrong parameter =invalid-params=, an unknown file, node or key =not-found=, an engine or document error =failed= with its message.',
  '=bt= edits =.rbt= behaviour trees (the =PATH= comes first). Node =props= and =params= are JSON objects: =props=\'{"seconds":2}\'=.',
  ['lrgeditorctl bt create ai/guard.rbt', 'lrgeditorctl bt add-node ai/guard.rbt wait parent=root props=\'{"seconds":2}\'',
   'lrgeditorctl bt layout ai/guard.rbt; lrgeditorctl bt validate ai/guard.rbt; lrgeditorctl bt save ai/guard.rbt'])
m('bt.open', '@path', 'path:s!', '={path}=', 'Open a .rbt in the behaviour tree editor.')
m('bt.create', '@path', 'path:s! name:s', '={path}=; =failed= when the file exists', 'Write a starter behaviour tree (a selector root) and open it; fails if the file exists.')
m('bt.get', '@path', 'path:s!', '={path, name, shown, root, nodes: [{id, explicit, type, kind, name, parent, index, yaml_path, props, params, x, y}], blackboard: [{name, type, default?}], subtrees: [{name, file}], selected, dirty, load_error?}=',
  'A behaviour tree: nodes (id, type, kind composite/decorator/leaf/subtree, parent, index, props, params, graph position), the blackboard schema, subtrees, the shown root (\"\" = main) and the selection.', ro=True)
m('bt.palette', '', '', '={types: [{name, kind}], actions, conditions}=', 'The node palette: built-in and registered node types with their kind, plus the registered action and condition bindings.', ro=True)
m('bt.add_node', '@path type', 'path:s! type:s! parent:s index:i id:s name:s props:o params:o x:n y:n', '={id}=',
  'Add a node of type under parent (an id; omitted = the new root of an empty tree) at index (-1 appends) with optional id, name, props, params and graph position.')
m('bt.remove_node', '@path id', 'path:s! id:s!', '={removed}=', 'Remove a node and the subtree under it.', de=True)
m('bt.move_node', '@path id parent', 'path:s! id:s! parent:s! index:i', '={id}=', 'Re-parent or reorder a node: new parent id and index.')
m('bt.set_node', '@path id', 'path:s! id:s! new_id:s name:v type:s props:o params:o', '={id}=', 'Change a node: new_id, name (null clears), type, props / params ({key: value}, null removes a key).')
m('bt.set_position', '@path id ~x ~y', 'path:s! id:s! x:n! y:n!', '={id}=', 'Move a node in the graph (kept in the file\'s editor: positions block).')
m('bt.layout', '@path', 'path:s!', '={nodes}=', 'Auto-layout the tree; positions are saved in the editor: block.')
m('bt.select', '@path ~id', 'path:s! id:v', '={selected}=', 'Select a node (null clears).')
m('bt.show', '@path ~subtree', 'path:s! subtree:v', '={shown}=', 'Show an inline subtree\'s root in the graph (null: the main root).')
m('bt.add_key', '@path name type', 'path:s! name:s! type:s! default:v', '={name}=', 'Add a blackboard schema key: type int, float, bool, string or object, optional default.')
m('bt.remove_key', '@path name', 'path:s! name:s!', '={removed}=', 'Remove a blackboard schema key.', de=True)
m('bt.set_subtree', '@path name', 'path:s! name:s! file:s', '={name}=', 'Define a subtree: with file a file subtree, else an inline one (an empty selector root).')
m('bt.remove_subtree', '@path name', 'path:s! name:s!', '={removed}=', 'Remove a subtree definition.', de=True)
m('bt.open_subtree', '@path id', 'path:s! id:s!', '={path}= for a file subtree (opened), ={shown}= for an inline one', 'Open the subtree a subtree node refers to: a file subtree opens in its own tab, an inline one is shown.')
m('bt.validate', '@path', 'path:s!', '={ok, issues: [{severity, path, line, message, node}]}=', 'Validate the tree (types, bindings, subtrees, schema); issues name node ids.', ro=True)
m('bt.save', '@path', 'path:s!', '={path}=', 'Save the .rbt.')
m('bt.live', '@path', 'path:s!', '={playing, attached, entity, status, tick, current, active_path, nodes: [{id, status, visited, ticks}]}=',
  'While playing: the runner of the selected entity\'s LrgAiComponent for this tree: per-node status, the active path and tick counts.', ro=True)

F('fsm', 'fsm', 'state machine editor', 'ipc-fsm', 'State machine methods',
  'Registered by the state machine editor (=src/editor/ui/data/lrg-editor-fsm-editor.c=). =path= is the =.rfsm= file (opened in its tab when needed). State paths are =a/b= (="= is the root, the top level); =from= ="= names the any-state transitions. Actions are JSON objects ={"action": NAME, "params": {}}=, ={"script": NAME, "params": {}}=, ={"set": KEY, "value": V}= or ={"event": NAME}=; conditions ={"key": KEY, "op": "eq\\vert{}ne\\vert{}lt\\vert{}le\\vert{}gt\\vert{}ge\\vert{}set\\vert{}unset", "value": V}=, ={"condition": NAME, "params": {}}= or ={"script": NAME, "params": {}}=. Every edit is one undo step of the document. Errors as for =bt.*=.',
  '=fsm= edits =.rfsm= state machines. Actions and conditions are JSON (see the IPC section).',
  ['lrgeditorctl fsm create ai/door.rfsm', 'lrgeditorctl fsm add-state ai/door.rfsm open; lrgeditorctl fsm add-transition ai/door.rfsm idle open event=use',
   'lrgeditorctl fsm set-actions ai/door.rfsm open enter actions=\'[{"event":"opened"}]\''])
m('fsm.open', '@path', 'path:s!', '={path}=', 'Open a .rfsm in the state machine editor.')
m('fsm.create', '@path', 'path:s! name:s', '={path}=; =failed= when the file exists', 'Write a starter state machine (an idle state) and open it; fails if the file exists.')
m('fsm.get', '@path', 'path:s! machine:s', '={path, name, machine, breadcrumbs, initial, blackboard, states: [{name, path, initial, children, enter, update, exit, x, y}], enter, update, exit (machine-wide), transitions: [{from, index, to, event, after, when, do, fires}] (every transition, the root\'s first, depth first), selected: {state, from, index}, dirty, load_error?}=',
  'A state machine as shown (machine: a state path whose sub-machine to list, default the shown one): states with enter/update/exit actions, transitions with event/after/when/do, blackboard schema, selection.', ro=True)
m('fsm.enter', '@path state', 'path:s! state:s!', '={machine}=', 'Show a state\'s sub-machine (\"\" = the top level).')
m('fsm.up', '@path', 'path:s!', '={machine}=', 'Show the parent machine (the breadcrumb\'s previous step).')
m('fsm.add_state', '@path name', 'path:s! name:s! parent:s x:n y:n', '={state}=', 'Add a state under parent (a state path, \"\" = top level) at an optional graph position.')
m('fsm.remove_state', '@path state', 'path:s! state:s!', '={removed}=', 'Remove a state; transitions targeting it go too.', de=True)
m('fsm.rename_state', '@path state name', 'path:s! state:s! name:s!', '={state}=', 'Rename a state; to: and initial: references follow.')
m('fsm.set_initial', '@path state', 'path:s! state:s!', '={state}=', 'Make a state the initial state of its machine.')
m('fsm.add_transition', '@path from to', 'path:s! from:s! to:s! event:s after:n when:a do:a', '={from, index}=', 'Add a transition from a state (\"\" = any state) to a state, fired by event and/or after seconds, guarded by when conditions, running do actions.')
m('fsm.set_transition', '@path from ~index', 'path:s! from:s! index:i! to:s event:v after:v when:a do:a', '={from, index}=', 'Change transition index of from: to, event (null clears), after (null clears), when, do.')
m('fsm.remove_transition', '@path from ~index', 'path:s! from:s! index:i!', '={removed}=', 'Remove transition index of from.', de=True)
m('fsm.set_actions', '@path state phase', 'path:s! state:s! phase:s! actions:a!', '={count}=', 'Replace a state\'s enter, update or exit action list.')
m('fsm.set_position', '@path state ~x ~y', 'path:s! state:s! x:n! y:n!', '={state}=', 'Move a state in the graph.')
m('fsm.select', '@path', 'path:s! state:s from:s index:i', '={state, from?, index?}=', 'Select a state ({state}) or a transition ({from, index}).')
m('fsm.add_key', '@path name type', 'path:s! name:s! type:s! default:v', '={name}=', 'Add a blackboard schema key (int, float, bool, string, object).')
m('fsm.remove_key', '@path name', 'path:s! name:s!', '={removed}=', 'Remove a blackboard schema key.', de=True)
m('fsm.validate', '@path', 'path:s!', '={ok, issues: [{severity, path, line, message}]}=', 'Validate the machine: states, initial names, transition targets, bindings.', ro=True)
m('fsm.save', '@path', 'path:s!', '={path}=', 'Save the .rfsm.')
m('fsm.live', '@path', 'path:s!', '={playing, attached, entity, running, active: [{state, time}], last_transition, queued (always empty: the engine does not expose the queue), fires: [{from, index, to, count}]}=',
  'While playing: the selected entity\'s running machine for this file: active states with times, the last transition, queued events and transition fire counts.', ro=True)
m('fsm.send_event', '@path event', 'path:s! event:s!', '={sent}=; =failed= when not playing or not attached', 'Queue an event on the attached live machine.')

F('ai', 'ai', 'AI panel', 'ipc-ai', 'AI debug methods',
  'Registered by the docked AI panel (=src/editor/ui/lrg-editor-ai-debug-panel.c=); live data only while playing. =entity= is the node GUID of a live entity made from a level node, else its name; omitted, the panel\'s selection. LLM decision brains appear once the embedding game or tool registers them with =lrg_editor_ai_debug_panel_watch_brain()=.',
  '=ai= reads and pokes the play world\'s AI: entities with an =LrgAiComponent=, their blackboards and LLM decision brains.',
  ['lrgeditorctl play start; lrgeditorctl ai entities', 'lrgeditorctl ai set-key alert true entity=Guard', 'lrgeditorctl ai brains; lrgeditorctl ai log limit=20'])
m('ai.show', '', '', '={shown}=', 'Show and raise the AI panel.')
m('ai.entities', '', '', '={playing, selected, entities: [{entity, name, tree, machine, current_node, current_state, running, keys, focused}]}=', 'While playing: entities with an LrgAiComponent, their tree/machine, current node/state and blackboard key count.', ro=True)
m('ai.select', '~entity', 'entity:v', '={selected}=; a name resolves to its live id (=not-found= without a match)', 'Select an entity in the AI panel by id or name (null follows the editor selection again).')
m('ai.state', '', 'entity:s', '={entity, debug, blackboard: [{key, type, value}]}=', 'An entity\'s AI debug state (YAML text of lrg_ai_component_dup_debug_state) and its live blackboard.', ro=True)
m('ai.set_key', 'key ~value', 'entity:s key:s! value:v! type:s', '={key, value}=', 'Set a live blackboard key (type int, float, bool or string; default the key\'s current type, else from the JSON value).')
m('ai.send_event', 'event', 'entity:s event:s!', '={sent}=', 'Send an event to an entity\'s state machine.')
m('ai.brains', '', '', '={brains: [{name, provider, model, asked, answered, cached, failed, input_tokens, output_tokens, mean_seconds, topics: [{topic, busy, pending_seconds, latency, asked, answered, cached, failed, error}]}]}=', 'The watched LLM decision brains (LrgDecisionBrain): counters, token totals, latency and per-topic state.', ro=True)
m('ai.topic', 'brain topic', 'brain:s! topic:s!', '={brain, topic, state, request}=', 'One brain topic: its state (YAML text) and the last request JSON.', ro=True)
m('ai.log', '', 'brain:s limit:i', '={entries: [{time, brain, kind, topic, text}]}=', 'The decision log (asked / answered / failed), newest last; limit default 50.', ro=True)

# ===========================================================================
# EDFX: particles.*, weather.*
# ===========================================================================
F('particles', 'particles', 'particle editor', 'ipc-particles', 'Particle effect methods',
  'Registered by =lrg_editor_particle_editor_install()= (=src/editor/ui/data/lrg-editor-particle-editor.c=). Every method takes =path= (the =.rparticles= file, absolute or project relative; opened in its editor tab when needed). =emitter= is an index or an emitter name, =module= an index in that emitter\'s stack. =value= is file syntax: a JSON number or boolean, a string exactly as written in the YAML file (="[4, 8]"= ranges, ="#ffc040"= colours, ="0 1; 1 0 ease-out-quad"= curves, ="0 #fff; 1 #0000"= gradients) or a JSON array of numbers; =null= resets to the default. Every edit is one undo step of the document. Errors: malformed parameters or an engine validation message =invalid-params=, an unknown file, emitter or module =not-found=, a file that does not load =failed=.',
  '=particles= edits =.rparticles= effects and drives the preview. =EMITTER= is an index or a name.',
  ['lrgeditorctl particles create fx/sparks.rparticles', 'lrgeditorctl particles add-module fx/sparks.rparticles 0 gravity',
   'lrgeditorctl particles set fx/sparks.rparticles lifetime \'"[0.5, 1.2]"\' emitter=0 module=3', 'lrgeditorctl particles seek fx/sparks.rparticles 1.5; lrgeditorctl particles stats fx/sparks.rparticles'])
m('particles.open', '@path', 'path:s!', '={path, panel}=', 'Open a .rparticles in the particle editor.')
m('particles.create', '@path', 'path:s! template:s', '={path}=; an existing file is =invalid-params=', 'Write a new effect: template default (one emitter: spawn-rate, shape, velocity, lifetime, color-over-life) or empty; opens it. Refuses an existing file.')
m('particles.get', '@path', 'path:s!', '={path, name, seed, duration, loop, prewarm, local_space, emitters: [{index, name, enabled, max_particles, start_delay, renderer, modules: [{index, kind, stage, enabled, gpu_capable, values}]}], unknown_keys, load_error, selected: {emitter, module}, dirty}=',
  'An effect: asset settings, emitters with their renderer block and module stack (values in file syntax), unknown keys, the selection.', ro=True)
m('particles.kinds', '', '', '={kinds: [{kind, stage, gpu_capable}]}=', 'The module kinds in stage order (lrg_particle_module_list_kinds).', ro=True)
m('particles.set', '@path key ~value', 'path:s! key:s! value:v! emitter:v module:i renderer:b', '={ok}=',
  'Set one key in file syntax: an asset key (no emitter), an emitter key (emitter), a renderer key (emitter + renderer: true) or a module key (emitter + module). null resets it.')
m('particles.add_emitter', '@path', 'path:s! name:s index:i', '={index, name}=', 'Add an emitter (name default "emitter", numbered when taken) at index.')
m('particles.remove_emitter', '@path ~emitter', 'path:s! emitter:v!', '={ok}=', 'Remove an emitter.', de=True)
m('particles.move_emitter', '@path ~from ~to', 'path:s! from:i! to:i!', '={ok}=', 'Reorder emitters.')
m('particles.duplicate_emitter', '@path ~emitter', 'path:s! emitter:v!', '={index, name}=', 'Duplicate an emitter.')
m('particles.add_module', '@path ~emitter kind', 'path:s! emitter:v! kind:s! index:i', '={index}=', 'Add a module of kind (see particles_kinds) to an emitter\'s stack at index (-1 appends).')
m('particles.remove_module', '@path ~emitter ~module', 'path:s! emitter:v! module:i!', '={ok}=', 'Remove a module from an emitter.', de=True)
m('particles.move_module', '@path ~emitter ~from ~to', 'path:s! emitter:v! from:i! to:i!', '={ok}=', 'Reorder an emitter\'s modules.')
m('particles.select', '@path ~emitter', 'path:s! emitter:v! module:i', '={emitter, module}=', 'Select an emitter and module (-1 none) for the property grid.')
m('particles.solo', '@path ~emitter', 'path:s! emitter:v!', '={solo}=', 'Preview only that emitter (and its sub-emitter targets); null clears. Not saved.')
m('particles.play', '@path ~playing', 'path:s! playing:b!', 'the preview state (as =particles.preview=)', 'Play or pause the preview.')
m('particles.restart', '@path', 'path:s!', 'the preview state', 'Restart the preview from time 0.')
m('particles.seek', '@path ~time', 'path:s! time:n!', 'the preview state', 'Seek the preview to time seconds (deterministic: restart and fixed 1/60 steps).')
m('particles.preview', '@path', 'path:s! speed:n seed:v ground:b background:s gpu:b', '={time, playing, speed, seed, ground, background, gpu, solo}=', 'Preview settings: speed (0.05..4), seed (null = the asset\'s), ground plane, background colour, GPU simulation.')
m('particles.stats', '@path', 'path:s!', 'the preview state plus ={active, bounds, emitters: [{name, enabled, active, capacity, bounds, gpu_ok, gpu_message, simulation, backend, drawn}]}=', 'Preview statistics: active particle counts and bounds per emitter, GPU capability.', ro=True)
m('particles.save', '@path', 'path:s!', '={path}=', 'Save the .rparticles.')
m('particles.render', '@path file', 'path:s! file:s! size:i', '={file, width, height}=; =failed= without a GL context', 'Render the preview to a PNG file (size default 512).')

F('weather', 'weather', 'weather editor', 'ipc-weather', 'Weather methods',
  'Registered by =lrg_editor_weather_editor_install()= (=src/editor/ui/data/lrg-editor-weather-editor.c=). =path= is the =.rweather= file (opened in its tab when needed); =preset= is an index or preset id, =layer= an index or layer id; =value= is file syntax as for =particles.*=. Every edit is one undo step of the document; errors as for =particles.*=.',
  '=weather= edits =.rweather= assets and previews presets and transitions.',
  ['lrgeditorctl weather create fx/sky.rweather', 'lrgeditorctl weather add-layer fx/sky.rweather storm rain',
   'lrgeditorctl weather set-param fx/sky.rweather storm 0 density 0.8', 'lrgeditorctl weather preview fx/sky.rweather preset=clear transition_to=storm'])
m('weather.open', '@path', 'path:s!', '={path, panel}=', 'Open a .rweather in the weather editor.')
m('weather.create', '@path', 'path:s! template:s', '={path}=; an existing file is =invalid-params=', 'Write a new weather asset: template default (clear and storm presets, a transition) or empty; opens it.')
m('weather.get', '@path', 'path:s!', '={path, dirty, load_error, name, default_preset, default_transition, presets: [{index, id, name, ambient_color, ambient_brightness, layers: [{index, kind, id, intensity, enabled, params}]}], transitions: [{from, to, duration}], selected: {preset, layer}, preview}=',
  'A weather asset: presets with their layers (kind, intensity, params in file syntax), transitions, selection.', ro=True)
m('weather.kinds', '', 'kind:s', '={kinds: [{kind, params: [{name, type, blurb, default, min?, max?, values?}]}]}=; an unknown kind is =not-found=', 'The layer kinds and their parameters (lrg_weather_asset_list_kind_params); kind narrows to one.', ro=True)
m('weather.set', '@path key ~value', 'path:s! key:s! value:v!', '={ok}=', 'Set an asset key: name, default-preset or default-transition.')
m('weather.add_preset', '@path id', 'path:s! id:s! name:s', '={index}=', 'Add a preset.')
m('weather.remove_preset', '@path ~preset', 'path:s! preset:v!', '={ok}=', 'Remove a preset.', de=True)
m('weather.set_preset', '@path ~preset', 'path:s! preset:v! name:s ambient_color:s ambient_brightness:n', '={ok}=', 'Change a preset: name, ambient_color ("#rrggbb"), ambient_brightness.')
m('weather.add_layer', '@path ~preset kind', 'path:s! preset:v! kind:s! id:s', '={index, id}=', 'Add a layer of kind (see weather_kinds) to a preset.')
m('weather.remove_layer', '@path ~preset ~layer', 'path:s! preset:v! layer:v!', '={ok}=', 'Remove a layer from a preset.', de=True)
m('weather.move_layer', '@path ~preset ~from ~to', 'path:s! preset:v! from:i! to:i!', '={ok}=', 'Reorder a preset\'s layers.')
m('weather.set_layer', '@path ~preset ~layer', 'path:s! preset:v! layer:v! intensity:n enabled:b', '={ok}=', 'Change a layer\'s intensity or enabled flag.')
m('weather.set_param', '@path ~preset ~layer name ~value', 'path:s! preset:v! layer:v! name:s! value:v!', '={ok}=', 'Set a layer parameter in file syntax (null unsets).')
m('weather.set_transition', '@path from to ~duration', 'path:s! from:s! to:s! duration:n!', '={ok}=', 'Set the crossfade duration between two presets.')
m('weather.remove_transition', '@path from to', 'path:s! from:s! to:s!', '={ok}=', 'Remove a transition rule.', de=True)
m('weather.select', '@path ~preset', 'path:s! preset:v! layer:v', '={preset, layer}=', 'Select a preset and layer.')
m('weather.preview', '@path', 'path:s! preset:v transition_to:s advance:n', '={active, transitioning, duration, remaining, wind_x, wind_y}=', 'Preview: switch to preset instantly ("none" clears), crossfade to transition_to with the resolved duration ("none" clears), and/or advance seconds.')
m('weather.save', '@path', 'path:s!', '={path}=', 'Save the .rweather.')
m('weather.render', '@path file', 'path:s! file:s! width:i height:i', '={file, width, height}=; =failed= without a GL context', 'Render the preview scene to a PNG file (width 640, height 360).')

# ===========================================================================
# EDTERR: terrain.*, water.*
# ===========================================================================
F('terrain', 'terrain', 'Terrain panel', 'ipc-terrain', 'Terrain methods',
  'Registered by the Terrain panel (=src/editor/ui/lrg-editor-terrain-panel.c=) once it is constructed. =node= is a node GUID, path or name; the default is the terrain node the panel shows (the primary selection when it is a =terrain= node). Every mutating method is one undo step of the level (=edit.undo=) unless noted; a stroke is one step however many dabs it has. Tool nicks: =select raise lower flatten smooth noise paint hole fill foliage-paint foliage-erase=; falloff nicks: =linear smooth sphere sharp constant curve=. Errors: missing or bad parameters (or no node selected) =invalid-params=, no such node, not a terrain node or an asset that does not load =not-found=, no Terrain panel, a save or load error or a limit =failed=. Actions: =terrain.show=, =terrain.create= (Create > Terrain), =terrain.create-water= (Create > Water), =terrain.save=, =terrain.rebake=, =terrain.tool-<nick>=.',
  '=terrain= sculpts and paints terrain nodes. =--node= (or =node==) picks the node; default the panel\'s. =stroke= takes =points=[[x,z],...]= in world units.',
  ['lrgeditorctl terrain create name=island', 'lrgeditorctl terrain brush tool=raise radius=6 strength=0.4',
   'lrgeditorctl terrain stroke points=[[0,0],[4,0],[8,2]] --node island', 'lrgeditorctl terrain add-layer name=sand tiling=8 --node island; lrgeditorctl terrain save --node island'])
m('terrain.get', '', 'node:s', '={node, name, asset, path, dirty, resolution, chunk_size, cell_size, chunks, cells, samples, size, height_range, splat_mode, holes, lod_levels, bake_resolution, layers: [{name, texture, material, tiling, color}], foliage: [{name, kind, mesh, texture, color, density, scale, slope, height, align, size, fade, seed, instances}], brush}=',
  'A terrain node: asset path and grid, height range, paint layers, foliage layers and the panel brush.', ro=True)
m('terrain.brush', '', 'tool:s radius:n strength:n falloff:s target_height:n sample_target:b noise_scale:n seed:i layer:i foliage_layer:i', 'the brush object (as in =terrain.get=); not undoable',
  'Read or change the panel brush (not undoable): tool, radius, strength, falloff, target_height, sample_target, noise_scale, seed, layer (surface layer painted), foliage_layer.')
m('terrain.stroke', '', 'node:s points:a! local:b tool:s amount:n invert:b smooth:b radius:n strength:n falloff:s target_height:n noise_scale:n seed:i layer:i foliage_layer:i', '={changed, dabs, tool, label?}=',
  'A scripted brush stroke along points [[x,z],...] (world x,z dropped onto the terrain, or local with local: true; off-terrain points are skipped) as ONE undo step "Terrain: <Op> stroke". tool defaults to the picked one (not select); amount per dab (1); invert = Ctrl (raise/lower, hole/fill, paint/erase), smooth = Shift; any brush field overrides for this stroke.')
m('terrain.sample', '~x ~z', 'node:s x:n! z:n! local:b', '={hit, height?, normal?, weights?, local?, world?, hole?, foliage_density?}=', 'Sample the terrain at x, z: height, normal, layer weights, hole, foliage densities.', ro=True)
m('terrain.add_layer', '', 'node:s name:s texture:s material:s tiling:n color:na', '={index}=; =failed= at 8 layers', 'Add a paint layer (name, texture, material, tiling, color [r,g,b(,a)]).')
m('terrain.remove_layer', '~index', 'node:s index:i!', '={removed}=; undo restores the layer and its weights; =failed= for the last layer', 'Remove a paint layer (undo restores its weights).', de=True)
m('terrain.set_layer', '~index', 'node:s index:i! name:s texture:s material:s tiling:n color:na', '={index}=', 'Change a paint layer.')
m('terrain.add_foliage', '', 'node:s name:s kind:s mesh:s lod_mesh:s lod_distance:n texture:s color:na density:n scale:na slope:na height:na align:v size:na fade:na seed:i', '={index}=', 'Add a foliage layer: kind mesh (default), billboard or grass, plus any set_foliage field.')
m('terrain.remove_foliage', '~index', 'node:s index:i!', '={removed}=; undo restores the layer and its density map', 'Remove a foliage layer.', de=True)
m('terrain.set_foliage', '~index', 'node:s index:i! mesh:s lod_mesh:s lod_distance:n texture:s color:na density:n scale:na slope:na height:na align:v size:na fade:na seed:i', '={index}=', 'Change a foliage layer\'s rules: meshes, LOD distance, texture, colour, density, scale/slope (degrees)/height ranges, align, size, fade distances, seed.')
m('terrain.rebake', '', 'node:s', '={instances}=; not undoable', 'Re-scatter foliage and drop chunk meshes and splat bakes (not undoable).')
m('terrain.save', '', 'node:s', '={path}=; not undoable', 'Write the .rterrain and its sidecars (also done on level save for dirty terrains).')
m('terrain.create', '', 'kind:s name:s', '={guid, name, asset}=', 'Create a terrain (kind terrain: <project>/terrain/<name>.rterrain, 4x4 chunks of 32 one-metre cells, grass and rock layers) or a water body (kind water: a 4-point lake .rwater) and add its node near the camera target, one undo step.')
m('terrain.show', '', '', '={panel, shown}=', 'Show and raise the Terrain panel.')

F('water', 'water', 'water editor', 'ipc-water', 'Water methods',
  'Registered by =lrg_editor_water_editor_install()= (=src/editor/ui/data/lrg-editor-water-editor.c=; the Terrain panel and the first =.rwater= editor call it). Each method takes either =path= (a =.rwater= file, opened in its data editor when needed: edits are steps of the document\'s undo history) or =node= (a level water node: edits the shared body as a level undo step; the file is marked dirty and written on level save, and an open editor of the same file follows). Errors as for =terrain.*=.',
  '=water= edits =.rwater= files (=path==) or level water nodes (=--node=).',
  ['lrgeditorctl water set path=water/lake.rwater level=1.5', 'lrgeditorctl water set-wave index=0 amplitude=0.3 --node Lake',
   'lrgeditorctl water add-point 4 0 width=3 --node Lake'])
m('water.get', '', 'path:s node:s', '={path, node?, name, shape, level, size, points: [[x, y, z, width]], cell_size, foam: [band, depth], waves: [{direction, amplitude, wavelength, speed, steepness}], wave_scale, colors: {shallow, deep, foam}, optics: {transparency, reflection, refraction, reflect_distance}, dirty}=', 'A water body: shape (plane, lake, river), level, size, points, waves, colours, optics, foam.', ro=True)
m('water.set', '', 'path:s node:s name:s shape:s level:n size:na cell_size:n wave_scale:n foam:na transparency:n reflection:n refraction:n reflect_distance:n colors:o', 'as =water.get=', 'Change a water body: name, shape, level, size [w,l], cell_size, wave_scale, foam [band,depth], transparency, reflection, refraction, reflect_distance, colors {shallow, deep, foam: [r,g,b(,a)]}.')
m('water.set_wave', '', 'path:s node:s count:i index:i direction:na amplitude:n wavelength:n speed:n steepness:n', 'as =water.get=', 'Set the wave count (0..4) and/or change wave index (direction [x,z], amplitude, wavelength, speed, steepness); needs index or count.')
m('water.add_point', '~x ~z', 'path:s node:s x:n! z:n! y:n width:n index:i', '={index}=', 'Add a lake/river point (x, z, y 0, width 4) before index (default append).')
m('water.set_point', '~index', 'path:s node:s index:i! x:n y:n z:n width:n', '={index}=', 'Move a lake/river point or change its width.')
m('water.remove_point', '~index', 'path:s node:s index:i!', '={removed}=', 'Remove a lake/river point.', de=True)
m('water.save', '', 'path:s node:s', '={path}=', 'Save the .rwater.')

# ===========================================================================
# EDAUDIO: mixer.*, audio.*
# ===========================================================================
F('mixer', 'mixer', 'mixer editor', 'ipc-mixer', 'Mixer methods',
  'Registered by =lrg_editor_mixer_editor_install()= (=src/editor/ui/data/lrg-editor-mixer-editor.c=; the Mixer panel and every mixer editor call it). Every method takes an optional =path= (a =.rmixer=); without it the project\'s default mixer (=project.ryaml= =audio: mixer:=), else =not-found= "No mixer: pass \\"path\\" or set the project\'s default mixer". Edits go through the file\'s mixer editor (opened as a tab when needed): one undo step each, the document stays dirty until =mixer.save=. Levels are dB (-144 .. 12.04). Most methods return the /mixer/: ={path, name, default_snapshot, dirty, buses: [{name, parent, volume, mute, solo, sends: [{target, level}], effects: [{id, type, enabled, params}]}], snapshots: [{name, blend, volumes, sends, params}], ducking: [{trigger, target, threshold, depth, attack, release}]}=. Errors: missing or mistyped parameters =invalid-params=, an unknown bus, effect, snapshot or file =not-found=, an engine refusal (a routing loop, ...) =failed= with the =LrgMixerAsset= message.',
  '=mixer= edits =.rmixer= bus graphs (=path== picks the file; default the project\'s mixer).',
  ["lrgeditorctl mixer create audio/main.rmixer buses='[\"music\",\"sfx\"]' set_default=true", 'lrgeditorctl mixer add-bus footsteps parent=sfx',
   'lrgeditorctl mixer add-effect sfx lowpass; lrgeditorctl mixer set-bus music volume=-6', 'lrgeditorctl mixer meters'])
m('mixer.show', '', '', '={panel, shown}=', 'Show and raise the Mixer panel.')
m('mixer.open', '', 'path:s', '={path, panel}=', 'Open a .rmixer (default the project\'s mixer) in the mixer editor.')
m('mixer.create', '@path', 'path:s! buses:sa set_default:b', 'the mixer', 'Write a new .rmixer: master plus the named buses; set_default makes it the project\'s mixer (project.ryaml audio.mixer).')
m('mixer.set_default', '~path', 'path:v!', '={mixer}=', 'Set (or with null clear) the project\'s default mixer; project.ryaml is saved.', de=True)
m('mixer.get', '', 'path:s', 'the mixer', 'A mixer: buses (parent, volume dB, mute, solo, sends, effect chain), snapshots, ducking rules.', ro=True)
m('mixer.effect_types', '', '', '={types: [{name, gtype, params: [{name, type, min, max, default, values}]}]}=', 'The bus effect types and their parameters.', ro=True)
m('mixer.add_bus', 'name', 'path:s name:s! parent:s', 'the mixer', 'Add a bus under parent (default master).')
m('mixer.remove_bus', 'name', 'path:s name:s!', 'the mixer', 'Remove a bus.', de=True)
m('mixer.set_bus', 'name', 'path:s name:s! volume:n mute:b solo:b parent:s', 'the mixer', 'Change a bus: volume (dB), mute, solo, parent (re-route).')
m('mixer.set_send', 'source target ~level', 'path:s source:s! target:s! level:n!', 'the mixer', 'Add or change a send from source to target at level dB.')
m('mixer.remove_send', 'source target', 'path:s source:s! target:s!', 'the mixer', 'Remove a send.', de=True)
m('mixer.add_effect', 'bus type', 'path:s bus:s! type:s! id:s', 'the mixer', 'Append an effect of type (see mixer_effect_types) to a bus.')
m('mixer.remove_effect', 'bus id', 'path:s bus:s! id:s!', 'the mixer', 'Remove an effect.', de=True)
m('mixer.move_effect', 'bus id ~index', 'path:s bus:s! id:s! index:i!', 'the mixer', 'Move an effect in its bus chain.')
m('mixer.set_effect', 'bus id', 'path:s bus:s! id:s! enabled:b params:o', 'the mixer', 'Enable/disable an effect or set its params ({name: value}, null unsets).')
m('mixer.add_snapshot', 'name', 'path:s name:s! blend:n capture:b', 'the mixer', 'Add a snapshot (blend seconds); capture copies the current bus volumes and send levels into it.')
m('mixer.remove_snapshot', 'name', 'path:s name:s!', 'the mixer', 'Remove a snapshot.', de=True)
m('mixer.set_snapshot', 'name', 'path:s name:s! blend:n default:b volumes:o sends:a params:o', 'the mixer', 'Change a snapshot: blend, default, volumes {bus: dB|null}, sends [{source, target, level|null}], params {"bus/effect/param": n|null}.')
m('mixer.capture_snapshot', 'name', 'path:s name:s!', 'the mixer', 'Overwrite a snapshot\'s volumes and sends with the current faders.')
m('mixer.set_duck', 'trigger target', 'path:s trigger:s! target:s! threshold:n depth:n attack:n release:n', 'the mixer', 'Add or change a ducking rule: trigger bus ducks target bus (threshold, depth dB, attack, release seconds).')
m('mixer.remove_duck', 'trigger target', 'path:s trigger:s! target:s!', 'the mixer', 'Remove a ducking rule.', de=True)
m('mixer.preview', '', 'path:s playing:b snapshot:v snapshots:sa weights:na blend:n', '={playing, snapshot, source}=', 'Offline preview with a test signal on every leaf bus: play/stop, switch to a snapshot, or blend several with weights.')
m('mixer.meters', '', 'path:s', '={source, buses: [{name, peak, rms, peak_db, rms_db, duck_gain, audible}]}=', "Peak/RMS meters per bus from the play session or the preview; path optional (without it the project mixer's open editor supplies the bus list).", ro=True)
m('mixer.save', '', 'path:s', '={path, saved}=', 'Save the .rmixer.')

F('audio', 'audio', 'inspector', 'ipc-audio', 'Audio emitter methods',
  'Registered by the inspector (=src/editor/ui/lrg-editor-inspector-panel.c=): the audio section of an =audio-emitter= node. =node= is a GUID or path. =params= keys: =min_distance=, =max_distance= (the radius), =rolloff_mode=, =rolloff=, =rolloff_curve_keys=, =cone_inner_angle=, =cone_outer_angle=, =cone_outer_gain=, =doppler_factor=, =speed_of_sound=, =occlusion=, =occlusion_gain=, =occlusion_cutoff=, =spatial_blend=, =bus=, =volume=.',
  '=audio= reads and sets an audio emitter node\'s attenuation, cone, doppler, occlusion and bus.',
  ['lrgeditorctl audio set Speaker min_distance=2 max_distance=30 bus=sfx', 'lrgeditorctl audio curve Speaker samples=16'])
m('audio.get', 'node', 'node:s!', '={node, kind, params}=', 'An audio emitter\'s source parameters (attenuation, cone, doppler, occlusion, bus, volume).', ro=True)
m('audio.set', 'node', 'node:s! params:o!', 'as =audio.get=; one undo step', 'Change an audio emitter\'s source parameters in one undo step.')
m('audio.curve', 'node', 'node:s! samples:i', '={min, max, mode, points: [[distance, gain]]}=', 'The emitter\'s distance attenuation curve sampled (2..256 points, default 32).', ro=True)
m('audio.buses', '', '', '={mixer, buses}=', 'The bus picker\'s list: the project mixer\'s buses.', ro=True)

# ===========================================================================
# ED2D: tiles.*, tileset.*, sprite.*
# ===========================================================================
F('tiles', 'tiles', 'Tiles panel', 'ipc-tiles', 'Tile painting methods',
  'Registered by the Tiles panel (=src/editor/ui/lrg-editor-tiles-panel.c=). =node= is a tilemap node (default the tiles target: the primary selection when it is a tilemap). Map cells and brush values are 1-based tile ids (0 = empty); tileset entries are 0-based tile indices. Mutating methods are one undo step of the level. Errors: bad parameters =invalid-params=, no target, tile or rule tile =not-found=, an engine error =failed=.',
  'The rule-tile and grid verbs of =tiles= (=tiles NODE paint|rect|fill|pick|configure= still drive =tilemap.*=): the first word is the verb, the node comes from =--node=, =node== or the selection. A node named like one of these verbs needs the =--node= form.',
  ['lrgeditorctl tiles brush rule_tile=grass rotate=1', 'lrgeditorctl tiles autopaint cells=[3,4,4,4,5,4] --node Ground', 'lrgeditorctl tiles grid orientation=isometric cell_size=64,32 --node Ground'])
m('tiles.get', '', 'node:s', '={node, tileset, tileset_kind, size, tile_size, grid: {orientation, stagger_axis, stagger_index, hex_side, cell_size} or null, layout, layers, brush: {tile, id, mirrored, rotation, rule_tile}, rule_tiles: [{name, default, n_rules}], tile}=', 'The tile target: tileset, grid (orthogonal/isometric/staggered/hexagonal), the brush and the rule tiles.', ro=True, verb='state')
m('tiles.grid', '', 'node:s orientation:s stagger_axis:s stagger_index:s hex_side:n cell_size:na', 'as =tiles.get=; one undo step; the node needs a grid (=tilemap.configure= first)', 'Change the tilemap grid: orientation, stagger_axis, stagger_index, hex_side, cell_size [w,h].')
m('tiles.brush', '', 'tile:i rule_tile:v mirrored:b rotation:i rotate:i flip_x:b flip_y:b', '={tile, id, mirrored, rotation, rule_tile}=; not undoable', 'The brush (not undoable): tile, rule_tile (name or null), flip/rotate bits (the X / Y / Z keys: flip_x, flip_y, rotate quarter turns).')
m('tiles.paint', '', 'node:s cells:na! rule_tile:s layer:i erase:b', '={changed}=; one undo step; creates the grid when missing', 'Paint cells [x,y,...]: with a rule tile (param or brush) autotiles them and their neighbours, else the brush value with its flip bits. One undo step.', verb='autopaint')
m('tiles.refresh', '', 'node:s layer:i', '={changed}=; one undo step; needs an =.rtileset=', 'Re-resolve every rule-tiled cell (layer index, default all; Refresh all). One undo step; needs an .rtileset.')
m('tiles.cell_at', '', 'node:s local:na!', '={cell, inside, polygon}= (local units, 4 or 6 points)', 'The cell under a local point (iso/hex aware picking, what the viewport uses).', ro=True)
m('tiles.tile_info', '~tile', 'node:s tile:i!', '={tile, index, flags, collision, data, animation, rule_tile}=', 'A tile\'s flags, collision polygons, custom data and animation.', ro=True)

F('tileset', 'tileset', 'tileset editor', 'ipc-tileset', 'Tileset methods',
  'Registered by the tileset editor (=src/editor/ui/data/lrg-editor-tileset-editor.c=). =path= is a =.rtileset= file, absolute or project relative (default the focused tileset editor). Tile indices are 0-based. Every mutating method is one undo step of the document (=data.undo=). Errors as for =tiles.*=.',
  '=tileset= edits =.rtileset= files: per-tile flags, data, collision, animation and rule tiles.',
  ['lrgeditorctl tileset create tiles/ground.rtileset image=tiles/ground.png tile_size=16,16', "lrgeditorctl tileset tile 5 flags='[\"solid\"]' path=tiles/ground.rtileset",
   'lrgeditorctl tileset rule-tile preset grass preset=blob47 first=16 path=tiles/ground.rtileset'])
m('tileset.create', '@path', 'path:s! image:s tile_size:na name:s', '={path}=; refuses to overwrite', 'Write a new .rtileset (tile_size default [16,16]; columns and tile count from the image size, 8x8 without one); refuses to overwrite.')
m('tileset.open', '@path', 'path:s!', '={path, panel}=', 'Open a .rtileset in the tileset editor.')
m('tileset.get', '', 'path:s', '={path, dirty, selected, selected_rule_tile, selected_rule, issues, error?, name, image, tile_size, columns, tile_count, margin, spacing, offset, tiles: [{id, flags, collision, data, animation}], rule_tiles: [{name, default, seed, border_is_this, match_tiles, rules: [{pattern, transform, tiles: [{tile, weight, rotate, flip}]}]}]}=', 'A tileset: grid, tiles with something set, rule tiles with their rules, selection.', ro=True)
m('tileset.set', '', 'path:s name:s image:s tile_size:na columns:i tile_count:i margin:i spacing:i offset:na', 'as =tileset.get=', 'Change the tileset: name, image, tile_size, columns, tile_count, margin, spacing, offset.')
m('tileset.select', '', 'path:s tile:i rule_tile:s rule:i page:s', '={selected, selected_rule_tile, selected_rule}=', 'Select a tile, rule tile or rule, and the page (tile or rules). Not an edit.')
m('tileset.tile', '~tile', 'path:s tile:i! flags:sa data:o animation:a', 'the tile entry ={id, flags, collision, data, animation}=', 'Set a tile\'s flags (solid, animated, hazard), custom data ({key: value|null}) or animation frames [{tile, duration}].')
m('tileset.collision', '~tile op', 'path:s tile:i! op:s! polygon:na index:i point:i x:n y:n', 'the tile entry', 'Edit a tile\'s collision polygons: op add (polygon [x0,y0,...]), clear, remove (index), move_point / insert_point / remove_point (index, point, x, y).')
m('tileset.rule_tile', 'op name', 'path:s op:s! name:s! new_name:s preset:s first:i default:v seed:i border_is_this:b', 'the rule tile entry (={removed}= for remove)', 'Rule tiles: op add, preset (edges16 / blob47 starting at tile first), remove, rename (new_name) or set (default: index, {tile, rotate, flip} or null; seed; border_is_this).')
m('tileset.rule', 'rule_tile op', 'path:s rule_tile:s! op:s! index:i to:i pattern:sa neighbor:s match:s transform:s tiles:a', 'the rule tile entry', "A rule tile's rules: op add, remove, move (index, to) or set: pattern [3 rows of '#','-','.','@'] or neighbor n..nw / all with match any/this/not-this, transform fixed/rotated/mirror-x/mirror-y/mirror-xy, tiles [index or {tile, weight, rotate, flip}].")
m('tileset.preview', 'rule_tile', 'path:s rule_tile:s! cells:na! size:na', '={size, tiles}= (cell values); no edit', 'Resolve a scratch map painted with the rule tile at cells [x,y,...] (size default [8,8]); no edit.', ro=True)
m('tileset.save', '', 'path:s', '={path}=', 'Save the .rtileset.')

F('sprite', 'sprite', 'sprite editor', 'ipc-sprite', 'Sprite sheet methods',
  'Registered by the sprite editor (=src/editor/ui/data/lrg-editor-sprite-editor.c=). =path= is a =.rsprite= file, absolute or project relative (default the focused sprite editor). Every mutating method is one undo step of the document. Errors as for =tiles.*=.',
  '=sprite= slices =.rsprite= sheets and edits their frames and animation clips.',
  ['lrgeditorctl sprite create sprites/hero.rsprite image=sprites/hero.png frame_size=32,32', 'lrgeditorctl sprite clip add walk frames=[0,1,2,3] fps=10 path=sprites/hero.rsprite',
   'lrgeditorctl sprite play clip=walk path=sprites/hero.rsprite'])
m('sprite.create', '@path', 'path:s! image:s name:s frame_size:na', '={path}=; refuses to overwrite', 'Write a new .rsprite for image; with frame_size grid-sliced.')
m('sprite.open', '@path', 'path:s!', '={path, panel}=', 'Open a .rsprite in the sprite editor.')
m('sprite.get', '', 'path:s', '={path, dirty, selected_frame, selected_clip, playing, time, preview_frame, preview_position, issues, error?, name, image, image_size, grid: {frame_size, columns, count} or null, frames: [{index, name, rect, pivot, grid}], clips: [{name, fps, loop, frames: [{frame, duration}], events: [{frame, name}], duration}]}=', 'A sprite sheet: image, grid, frames (rect, pivot), clips (fps, loop, frames, events) and the preview state.', ro=True)
m('sprite.slice', 'mode', 'path:s mode:s! frame_size:na columns:i rows:i count:i margin:na spacing:na pivot:na threshold:i min_size:i', 'as =sprite.get=', 'Re-slice the sheet: mode grid (frame_size, columns, rows, count, margin [x,y], spacing [x,y], pivot [x,y]) or alpha (opaque islands: threshold 0, min_size 2, pivot fractions [0.5,1]).')
m('sprite.frame', 'op', 'path:s op:s! index:i rect:na name:s pivot:na', 'the frame entry (={removed}= for remove)', 'Frames: op add (rect [x,y,w,h]), set, remove or select (index); name, pivot [x,y]. Grid frames only take name and pivot.')
m('sprite.clip', 'op name', 'path:s op:s! name:s! new_name:s fps:n loop:s frames:a', 'the clip entry', 'Clips: op add, remove, set or select (new_name, fps, loop once/loop/ping-pong, frames [index|name|{frame, duration}]; set replaces the list).')
m('sprite.clip_frame', 'clip op', 'path:s clip:s! op:s! index:i to:i frame:v duration:n', 'the clip entry', 'A clip\'s frames: op add, insert, remove, move (index, to) or set (frame, duration).')
m('sprite.event', 'clip op ~frame', 'path:s clip:s! op:s! frame:i! name:s', 'the clip entry', 'Clip events: op add (frame = clip position, name) or remove.')
m('sprite.play', '', 'path:s clip:s playing:b time:n', '={clip, playing, time, frame, position}=; not an edit', 'The preview player (not an edit): clip, play/pause, time.')
m('sprite.save', '', 'path:s', '={path}=', 'Save the .rsprite.')

# ===========================================================================
# EDPLUMB: asset.new
# ===========================================================================
F('assetnew', 'asset', 'assets panel', 'asset-file-methods', 'Asset creation', '', '', [])
m('asset.new', 'kind', 'kind:s! path:s name:s image:s', '={path, relative, is_dir, guid, kind}=',
  'Create a new asset file: kind is a document format nick (bt, fsm, particles, weather, mixer, tileset, sprite, water, nav-settings, material, ...) or terrain. path (project relative or absolute) or, without it, a free <name>.<ext> in the assets panel\'s folder. kind sprite with image writes an auto-sliced .rsprite of that image.', verb='new')

# ===========================================================================
# 0.9.0 M8 (m8-ipc/*.md): CODE debug.*, lsp.*
# ===========================================================================
_POS = '=line= and =column= are 1-based; =path= is a script file, absolute or project relative (default the code panel\'s active tab).'
F('debug', 'debug', 'Debug panel', 'ipc-debug', 'Script debugger methods',
  'Registered by the docked Debug panel (=src/editor/ui/lrg-editor-debug-panel.c=) on top of =LrgScriptDebugger=. ' + _POS + ' Stack, scopes, variables and evaluate need a paused session. A DAP client (Emacs dape, VS Code) can attach to the port =debug.dap_start= opens. Errors: missing or bad parameters =invalid-params=; an unknown breakpoint, frame, variable reference or file =not-found=; no debug session, a paused-only call while running or an engine error =failed=. Details: [[file:debugging.org::#editor-debug-ipc][Debugging]].',
  '=debug= drives the script debugger: breakpoints, stepping, the stack and variables, watches and the DAP server.',
  ['lrgeditorctl debug set-breakpoint scripts/player.lua 12 condition=\'hp < 10\'', 'lrgeditorctl play start; lrgeditorctl debug state',
   'lrgeditorctl debug stack; lrgeditorctl debug scopes; lrgeditorctl debug variables 1', 'lrgeditorctl debug evaluate \'self.hp * 2\'; lrgeditorctl debug step-over',
   'lrgeditorctl debug dap-start port=4711   # then M-x libregnum-dape'],
  wave='M8')
m('debug.state', '', '', '={state, reason, path, line, language, message, dap: {listening, port, clients}}=; =state= idle, running or paused; =reason= breakpoint, step, pause, exception, entry or null',
  'The script debugger\'s state: idle/running/paused, why it stopped (breakpoint, step, pause, exception, entry), where (path, line, language), the stop message and the DAP server (listening, port, clients).', ro=True)
m('debug.show', '', '', '={}=', 'Show and raise the Debug panel.')
m('debug.breakpoints', '', 'path:s', '=[{id, path, line, condition, hit_condition, log_message, enabled, verified, hits}]=', 'The breakpoints (of one file with path): id, path, line, condition, hit condition, log message, enabled, verified, hit count.', ro=True)
m('debug.set_breakpoint', '@path ~line', 'path:s! line:i! condition:s hit_condition:s log_message:s enabled:b', 'the breakpoint; replaces one at the same =path:line=',
  'Set a breakpoint at path:line (replacing one there): condition (an expression in the script language), hit_condition (e.g. ">= 3"), log_message (a log point: logs instead of stopping, {expr} interpolated), enabled. Returns the breakpoint.')
m('debug.remove_breakpoint', '@path ~line', 'path:s! line:i!', '={removed}=', 'Remove the breakpoint at path:line.', de=True)
m('debug.toggle_breakpoint', '~line', 'path:s line:i!', '={set, breakpoint}= (=breakpoint= null when removed)', 'Toggle a breakpoint at line of path (default the active tab), as a gutter click does.')
m('debug.clear_breakpoints', '', 'path:s', '={removed}= (a count)', 'Remove every breakpoint (of one file with path).', de=True)
m('debug.continue', '', '', 'as =debug.state=', 'Resume a paused script.')
m('debug.pause', '', '', 'as =debug.state=', 'Pause the running script at its next line.')
m('debug.step_over', '', '', 'as =debug.state=', 'Step to the next line in the same frame.')
m('debug.step_in', '', '', 'as =debug.state=', 'Step into the call on the current line.')
m('debug.step_out', '', '', 'as =debug.state=', 'Run until the current frame returns.')
m('debug.stop', '', '', 'as =debug.state=', 'End the debug session: clears the stop and detaches; play keeps running.', de=True)
m('debug.stack', '', '', '=[{id, name, path, line, column, language}]= (paused only)', 'The paused call stack, innermost first: frame id, function name, path, line, column, language.', ro=True)
m('debug.scopes', '', 'frame:i', '=[{name, reference, expensive}]=', 'The scopes (locals, upvalues/closure, globals) of a frame (default the top): name and a variables reference.', ro=True)
m('debug.variables', '~reference', 'reference:i!', '=[{name, value, type, reference}]= (=reference= 0 for a leaf)', 'The variables under a reference from debug.scopes or a parent variable (tables, dicts, GObjects expand lazily; reference 0 = leaf).', ro=True)
m('debug.evaluate', 'expression', 'expression:s! frame:i', '={result, type, reference}=', 'Evaluate an expression in a paused frame (default the top) in the script\'s language; reference > 0 can be expanded with debug.variables.')
m('debug.watches', '', '', '=[{expression, value, type, error}]=', 'The watch expressions with their current values (or errors).', ro=True)
m('debug.add_watch', 'expression', 'expression:s!', 'as =debug.watches=', 'Add a watch expression. Returns the watches.')
m('debug.remove_watch', 'expression', 'expression:s!', 'as =debug.watches=', 'Remove a watch expression. Returns the watches.')
m('debug.dap_start', '', 'port:i host:s', '={port}=', 'Start the Debug Adapter Protocol server for external debuggers (Emacs dape, VS Code): port 0 picks a free one, default the debug.dap-port setting (4711); host default 127.0.0.1. Returns {port}.')
m('debug.dap_stop', '', '', '={}=', 'Stop the DAP server (disconnects its clients).')

F('lsp', 'lsp', 'code panel', 'ipc-lsp', 'Language server methods',
  'Registered by the code panel (=src/editor/ui/lrg-editor-code-panel.c=): the editor\'s LSP client (=LrgLspClient=) with one server per language from the editor settings (clangd for crispy C, lua-language-server, pylsp or pyright, typescript-language-server for GJS); a language without an installed server answers with empty results. ' + _POS + ' Position methods default to the caret. Errors as for =debug.*= (a crashed server or a timed-out request is =failed=). Details: [[file:code-intelligence.org::#editor-lsp-ipc][Code intelligence]].',
  '=lsp= asks the code panel\'s language servers: status, diagnostics, completion, hover, definition, references, rename and formatting.',
  ['lrgeditorctl lsp servers', 'lrgeditorctl code open scripts/player.lua:12; lrgeditorctl lsp diagnostics',
   'lrgeditorctl lsp hover line=12 column=7; lrgeditorctl lsp definition line=12 column=7 jump=true', 'lrgeditorctl lsp rename hit_points line=3 column=7'],
  wave='M8')
m('lsp.servers', '', '', '=[{language, command, available, running, initialized, binary}]=', 'The configured language servers: language, command, whether the binary is installed (available, binary path), running and initialized.', ro=True)
m('lsp.status', '', 'path:s', '={path, language, server, running, initialized, version, diagnostics}=', 'The language server state of a file: its language, server, document version and diagnostic count.', ro=True)
m('lsp.diagnostics', '', 'path:s', '=[{line, column, end_line, end_column, severity, message, source, code}]=', 'The diagnostics the server published for a file: range, severity (error, warning, info, hint), message, source, code.', ro=True)
m('lsp.complete', '', 'path:s line:i column:i', '=[{label, kind, detail, insert_text}]=', 'Completion items at a position (default the caret): label, LSP kind, detail, insert text.', ro=True)
m('lsp.hover', '', 'path:s line:i column:i', '={contents}=', 'Hover text at a position (default the caret); contents null when there is none.', ro=True)
m('lsp.definition', '', 'path:s line:i column:i jump:b', '=[{path, line, column}]=', 'Where the symbol at a position is defined; jump: true opens the first location in the code panel.')
m('lsp.references', '', 'path:s line:i column:i', '=[{path, line, column}]=', 'Every reference to the symbol at a position.', ro=True)
m('lsp.rename', 'new_name', 'path:s line:i column:i new_name:s!', '={files, edits}=', 'Rename the symbol at a position across the project: edits open files\' buffers (one undo step per buffer). Returns {files, edits}.')
m('lsp.format', '', 'path:s', '={edits}=', 'Format a file with its language server (one undo step).')
m('lsp.restart', '', 'language:s', 'as =lsp.servers=', 'Restart the language server of a language (default every running one). Returns lsp_servers.')

# ===========================================================================
# 0.9.0 M8: PROF profiler.*, frame.*
# ===========================================================================
F('profiler', 'profiler', 'profiler panel', 'ipc-profiler', 'Profiler capture methods',
  'Registered by the profiler panel (=src/editor/ui/lrg-editor-profiler-panel.c=) next to =profiler.sample= (see [[#profiler-methods][Profiler]]). A frame =index= is the profiler\'s monotonic frame number (not a ring slot). Times: =*_us= integer microseconds from the frame start, =*_ms= doubles. =source= is =live= (the profiler\'s ring) or =capture= (the panel\'s capture: the last =profiler.capture= or =profiler.load=); default =capture= when one is held, else =live=. =path= is absolute or project relative; captures are =.rprof= files. Errors: missing or bad parameters =invalid-params=; no such frame or no capture yet =not-found=; I/O or parse errors =failed=. Details: [[file:profiling.org::#editor-profiling-ipc][Profiling]].',
  '=profiler= reads the CPU/GPU profiler: a bare =profiler [frames=N] [enable=BOOL]= (or =profiler sample=) prints the section table of =profiler.sample=; the other verbs switch it, capture and load =.rprof= files, list and compare frames and drive the panel.',
  ['lrgeditorctl profiler set enable=true gpu=true', 'lrgeditorctl profiler capture frames=240 path=captures/boss.rprof',
   'lrgeditorctl profiler frames count=10; lrgeditorctl profiler frame index=1234', 'lrgeditorctl profiler compare a=1200 b=1234',
   'lrgeditorctl profiler view mode=flame frame=1234 show=true'],
  wave='M8', hybrid=['sample'])
m('profiler.set', '', 'enable:b gpu:b paused:b ring:i', '={enabled, pinned, paused, gpu, gpu_supported, ring}=', 'Switch the profiler: enable, gpu (the GPU timer-query lane, when supported), paused, ring (frames kept, 8..4096).')
m('profiler.capture', '', 'frames:i path:s', '={frames, first, last, events, threads, path}=', 'Snapshot the last frames (default 120) of the ring into the panel\'s capture; with path also save it as .rprof.')
m('profiler.save', '@path', 'path:s!', '={path, frames, events}=', 'Save the panel\'s capture as a .rprof file (not-found without a capture).')
m('profiler.load', '@path', 'path:s!', '={path, frames, first, last, events, threads}=', 'Load a .rprof capture; it becomes the panel\'s capture and the panel shows it.')
m('profiler.frames', '', 'source:s count:i', '=[{index, start_us, duration_ms, gpu_ms, events, counters}]= (newest last)', 'Frame summaries (newest last) from the live ring or the capture: index, start, CPU and GPU time, event and counter counts.', ro=True)
m('profiler.frame', '', 'source:s index:i', '={index, start_us, duration_ms, gpu_ms, threads: [{id, name}], events: [{name, thread, depth, parent, start_us, duration_us}], gpu: [{name, depth, start_us, duration_us}], counters: [{name, value}]}=',
  'One frame in full (default the newest): threads, nested CPU section events (parent -1 = root), GPU lane events and counters.', ro=True)
m('profiler.compare', '', 'source:s a:i! b:i!', '={a: {index, duration_ms}, b: {index, duration_ms}, sections: [{name, a_ms, b_ms, delta_ms, a_calls, b_calls}]}=', 'Compare two frames: per section name the inclusive time and calls in each, sorted by the largest difference.', ro=True)
m('profiler.view', '', 'mode:s frame:i compare:i show:b', '={mode, frame, compare, source}=', 'Drive the profiler panel: mode timeline, flame, table or compare; the frame (and the compare frame) it shows; show presents the panel.')

F('frame', 'frame', 'Frame Debugger panel', 'ipc-frame', 'Frame debugger methods',
  'Registered by the docked Frame Debugger panel (=src/editor/ui/lrg-editor-frame-debugger.c=) on =LrgFrameCapture=. =frame.capture= arms a capture on the viewport\'s =LrgWorld3DRenderer=; the next viewport render is recorded (poll =frame.status=). Calls are numbered from 0 in draw order; =pass= is a render pass nick. Scrubbing and the replay image need the live renderer (a loaded =.rframe= is browse-only). Errors: missing or bad parameters =invalid-params=; no capture, call or texture =not-found=; no GPU or a failed read-back =failed=. Details: [[file:profiling.org::#editor-profiling-frame][Frame debugger]].',
  '=frame= captures one viewport frame\'s draw calls, lists and inspects them, scrubs the viewport to call N and saves textures and render targets as PNG.',
  ['lrgeditorctl frame capture; lrgeditorctl frame status', 'lrgeditorctl frame calls pass=opaque limit=20; lrgeditorctl frame call 17',
   'lrgeditorctl frame scrub 17; lrgeditorctl frame texture /tmp/partial.png target=replay', 'lrgeditorctl frame save captures/hall.rframe'],
  wave='M8')
m('frame.capture', '', '', '={armed}=', 'Arm a capture: the next viewport render records its draw calls (poll frame_status).')
m('frame.status', '', '', '={armed, captured, calls, width, height, scrub, passes: [{pass, calls}]}= (=scrub= -1 = the full frame)', 'The capture state: armed, captured, call count, size, scrub position (-1 = full frame) and calls per pass.', ro=True)
m('frame.calls', '', 'pass:s offset:i limit:i', '={total, calls: [{index, pass, kind, entity, guid, mesh, material, shader, vertices, triangles}]}=', 'The captured draw calls (of one pass), paged by offset and limit (default 200).', ro=True)
m('frame.call', '~index', 'index:i!', '={index, pass, kind, entity, guid, mesh, material, shader, shader_id, texture_id, vertices, triangles, instances, color, transform, state: {blend, blend_mode, depth_test, depth_write, cull, wireframe, framebuffer}}=',
  'One draw call in full: what drew it (entity, node guid, mesh, material, shader), counts, colour, transform and the render state.', ro=True)
m('frame.scrub', '~index', 'index:i!', '={scrub, calls}=', 'Show the frame up to call index (-1 = the full frame) in the viewport and the replay image.')
m('frame.texture', '@path', 'path:s! id:i target:s', '={path, width, height}=', 'Save a texture (GL id) or render target (target replay, shadow, depth, normal or ssao; replay = the partial frame at the scrub) as a PNG.')
m('frame.save', '@path', 'path:s!', '={path, calls}=', 'Save the capture as a .rframe YAML file.')
m('frame.load', '@path', 'path:s!', 'as =frame.status=', 'Load a .rframe capture to browse offline (scrub and replay need the live renderer).')
m('frame.show', '', '', '={}=', 'Show and raise the Frame Debugger panel.')

# ===========================================================================
# 0.9.0 M8: TOOLSCRIPT script.*
# ===========================================================================
F('script', 'script', 'console panel', 'ipc-script', 'Tool script methods',
  'Registered by the console panel (=src/editor/ui/lrg-editor-console-panel.c=): editor-time tool scripts run against the global =editor= object (=LrgEditorScriptApi=). =language= is =lua=, =python=, =pygobject= or =gjs= (only the ones built in; others are =invalid-params=). Every run is one undo step. =script.run= with =code= runs in that language\'s persistent console context (REPL state is kept); with =path= in a fresh context. Tools are the menu items scripts in =<project>/editor_scripts/= register on project open. (The node script bindings =script.add=, =script.remove=, =script.open= are [[#document-methods][document methods]].) Errors: missing or bad parameters =invalid-params=; a missing file, tool, node or binding =not-found=; a script error =failed= with =file:line: message=. Details: [[file:tool-scripts.org][Tool scripts]].',
  '=script= runs editor tool scripts and manages the =editor_scripts/= tools and run-in-editor script bindings.',
  ["lrgeditorctl script run 'editor:log(\"hi\")'", 'lrgeditorctl script run path=editor_scripts/scatter.lua',
   "lrgeditorctl script run 'editor.notify(\"hi\")' language=python", 'lrgeditorctl script tools; lrgeditorctl script run-tool script.tool.scatter',
   'lrgeditorctl script run-in-editor Spinner enabled=true; lrgeditorctl script editor-running'],
  wave='M8')
m('script.run', 'code', 'code:s path:s language:s label:s', '={result, output, language}=', 'Run a tool script as one undo step (label names it): code in the language\'s persistent console context, or the file at path in a fresh one; language default from the file extension, else lua. Returns the result value, captured output and language.')
m('script.languages', '', '', '=[{id, name, available}]=', 'The tool script languages and whether each is built in.', ro=True)
m('script.tools', '', '', '=[{id, label, menu, script, language, function, shortcut}]=', 'The tools registered by editor_scripts/ files: action id (script.tool.<name>), label, menu path, script, language, callback, shortcut.', ro=True)
m('script.run_tool', 'id', 'id:s!', '=true=', 'Run a registered tool by its action id (script.tool.<name>).')
m('script.reload', '', '', '={dir, loaded, failed: [{path, message}]}=', 'Re-run every <project>/editor_scripts/ file (re-registering their tools).')
m('script.run_in_editor', 'node', 'node:s! index:i enabled:b!', '=true=; one undo step', 'Make a node\'s script binding (index, default 0) tick while authoring (the run-in-editor property, like Godot\'s @tool); one undo step.')
m('script.editor_running', '', '', '=[{node, script, language, error}]=', 'The script bindings ticking in the editor (run-in-editor), with their last error.', ro=True)
