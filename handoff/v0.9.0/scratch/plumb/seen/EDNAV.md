# FINAL - EDNAV IPC methods

Registered by src/editor/ui/lrg-editor-nav-panel.c. All work on the open level (error FAILED
"No level is open" without one). Errors: INVALID_PARAMS for missing/bad params, NOT_FOUND for an
unknown area/box/link index or name or a missing file, FAILED for engine errors (bake, load, save).
Every mutating method (except bake/cancel/overlay/select/debug_agent) is ONE undo step in the level
history (`edit.undo` reverts it). The panel's state = the level's `navigation:` block + the in-memory
.rnav settings (LrgNavBake); `dirty` says the settings differ from the .rnav file on disk.

Vectors are [x,y,z]. Area refs: `area` = index (int) or name (string).

## nav.*
- nav.get {} -> {settings:str|null, settings_path:abs|null, data:str|null, bake_on_load:bool, dirty:bool,
  bake:{name, agent:{radius,height,max_slope,max_step,max_drop}, voxel:{cell_size,cell_height},
  regions:{min_area,corner_margin}, sources:{meshes,colliders,static_only,layer_mask,include_tags:[],
  exclude_tags:[],physics_layers:[],bounds:{min,max}|null}, output:str|null},
  areas:[{index,name,tag,cost,color:"#rrggbb",boxes:[{min,max}]}],
  links:[{index,start,end,bidirectional,cost,area:str|null}],
  mesh:{present,unsaved,polygons,regions,links,boundaries,triangles,seconds},
  baking:bool, playing:bool, overlay:[nicks], selection:{area,box,link} (-1 = none)}
- nav.set {key, value} | {values:{key:value,...}} -> nav.get
  keys: name, agent_radius, agent_height, max_slope, max_step, max_drop, cell_size, cell_height,
  min_region_area, corner_margin, include_meshes, include_colliders, static_only, layer_mask,
  include_tags, exclude_tags, physics_layers ([str] or "a, b"), bounds ({min,max} or null), output,
  settings (path of an existing .rnav: loads it as the level's settings), data (str|null),
  bake_on_load (bool). '-' and '_' are interchangeable. One undo step per call.
- nav.new {path?} -> nav.get   (writes a default .rnav (default <level-stem>.rnav next to the level)
  and makes it the level's `navigation: settings:`; the ref change is undoable, the file stays)
- nav.bake {async?:false, save?:bool (default: true when the level has a file)} ->
  {polygons, regions, links, boundaries, triangles, seconds, saved:bool}  (async: {started:true})
- nav.cancel {} -> {cancelled:bool}   (an async bake's result is dropped)
- nav.save {} -> nav.get   (writes the .rnav and the baked .rnavmesh (output, else <level-stem>.rnavmesh),
  sets settings/data refs as one undo step)
- nav.clear {} -> {cleared:bool}   (drops the baked mesh and the `data:` ref; undoable)
- nav.area.add {name, tag?:auto, cost?:1} -> {index}
- nav.area.remove {area} -> {removed:true}   (links naming it lose their area)
- nav.area.set {area, name?, tag?, cost?} -> {index}
- nav.area.box.add {area, min, max} -> {index}
- nav.area.box.remove {area, box} -> {removed:true}
- nav.area.box.set {area, box, min?, max?} -> {index}
- nav.link.add {start, end, bidirectional?:true, cost?:0, area?:str} -> {index}
- nav.link.remove {index} -> {removed:true}
- nav.link.set {index, start?, end?, bidirectional?, cost?, area?:str|null} -> {index}
- nav.select {area?, box?, link?} -> {area, box, link}   (what the viewport handles edit; -1 clears)
- nav.overlay {flags?:[nicks] (replace), on?:[nicks], off?:[nicks]} -> {flags:[nicks]}
  nicks: polygons, polygon-edges, boundaries, portals, links, areas, agents, paths, velocities, orca,
  neighbours, handles, all, default (default/all are combinations). Not undoable (a view setting).
- nav.agents {} -> {playing:bool, step_seconds, step_count, debug:index|null,
  agents:[{index, guid|null, node|null (path), entity, state:"idle"|"moving"|"on-link"|"arrived"|"failed",
  position, velocity, speed, remaining, destination|null, path:[[x,y,z],...], corner}]}
  (empty agents when not playing)
- nav.debug_agent {index?:int | guid?:str | node?:str | clear?:true} -> {debug:index|null}
  (lrg_nav_crowd_set_debug_agent on the play world's crowd; the panel's agent list selects the same)
- nav.show {} -> {panel:"navigation", shown:bool}

## Actions (registered from the panel file)
nav.show (Navigation Panel), nav.bake (Bake Navigation), nav.cancel, nav.save (Save Navigation),
nav.clear (Clear Baked Navigation), nav.new-settings (New Navigation Settings),
nav.open-settings (shows the panel; for EDPLUMB: to open a .rnav from the data editor call
`lrg_editor_nav_panel_open_settings (lrg_editor_nav_panel_find (app), path, &error)` then
`lrg_editor_app_activate_action (app, "nav.open-settings")`),
nav.overlay-polygons / -areas / -links / -boundaries / -agents / -paths / -orca / -handles (checkable
toggles for the viewport overlay menu).
