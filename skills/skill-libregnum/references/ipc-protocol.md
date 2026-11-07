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

Application: `action.list|run`, `view.get|set|focus|screenshot` (all take `viewport` 0..3, default the active one), `play.start|stop|pause|step|state`, `run.start|stop`, `project.info|open`, `asset.list`, `asset.thumbnail {path, wait}`, `level.validate|import`, `ui.notify {message, level: info|success|warning|error}` (0-3 accepted for old clients), `ui.state`, `tilemap.tool`, `timeline.play|stop|seek|state` (preview; never saved), `editor.quit`. The `view.*`, `play.*`, `timeline.*` and `tilemap.tool` methods act on the headless view model, so they work with `lrgeditor --headless`.

Added in 0.8.1 (protocol version still 1; no 0.8.0 method or reply changed; 130 methods in all, `methods` lists them):

- Headless, on every server: `asset.meta.get|set {path, settings?, guid?}` (`.meta` sidecar: guid, importer, settings; `set` is all or nothing), `vcs.status {path?}` (`repository`, `branch`, `files [{path status letter}]`), `prefab.overrides|apply|revert|relink|unlink {node, keys?, path?}` and `prefab.sync {path?}` (linked prefab instances; override keys like `transform/scale` or `<guid>:path`).
- Viewport split: `view.layout {layout?: single|split-h|split-v|quad}`, `view.render_mode {viewport?, mode?: lit|unlit|wireframe|normals|lighting-only}`, `view.overlay {viewport?, name?, enabled?}` (grid, gizmos, icons, bounds, stats, scene-lighting, shadows, sky, post).
- Hierarchy panel: `hierarchy.reveal|rename {node|guid, name?, select?}`, `hierarchy.filter {text}`, `hierarchy.state`.
- Inspector panel: `inspector.list|get|set|reset|revert_override|apply_override {node|nodes, section: header|transform|visual|component|scripts, component?, name, value?}` (`set` on several nodes is one undo step), `inspector.state`.
- Assets panel: `asset.reveal|rename|move|duplicate|trash {path, name?, dest?}`, `asset.orphans {trash?}` (the `.meta` follows its asset).
- Code panel: `code.open {path, line?}`, `code.list`, `code.save {path?}`, `palette.open {mode?: commands|files, query?}`, `preferences.open {page?}`.
- Data editors: `data.open|list|get|set|edit|undo|redo|validate|save|reload|close|reveal {path?, at?, value?|yaml?, op?}` on input maps, locales, sound banks, themes, HUD and UI layouts, dialog trees and dungeons (`path` omitted = the focused data editor).
- Profiler panel: `profiler.sample {frames?, enable?}`.

`ui.state` keeps every 0.8.0 field (`menu`, `menubar` rects, `frame_height`, `dialog`, `palette`, `nav`, `viewport`, `viewport_hovered`, `window`, `fps`, `input`) and adds `viewports [{index rect render_mode mode_2d}]`, `active_viewport`, `layout`, `panels` and `focus`. The full table with parameters, results and errors is the method index in `docs/editor/ipc-protocol.org`.

## Events

`changed {dirty, undo, redo, nodes, last}`, `selection {nodes, primary}`, `level {path, name}`, `saved {path}`, `tool {tool, space, snap}`, `log {serial, time, level, domain, message}`, `play`, `run`, `open-file {file, line, col}`.

## From bash

```bash
sock="$(ls -t "${XDG_RUNTIME_DIR}"/libregnum/editor-*.sock | head -n 1)"
printf '{"id":1,"method":"node.find","params":{"query":"Door"}}\n' \
	| socat -t 2 - UNIX-CONNECT:"${sock}" | yq -p json '.result'
```

## Materials and shaders (0.9.0)
Registered by the assets panel (material.*, shadergraph.*) and the code panel (shader.*); full tables in docs/editor/ipc-protocol.org#material-methods.
material.open {path} -> {path,panel}; material.create {path, parent?, shading_model?: lit|unlit|custom, shader?} -> {path}; material.create_variant {path, dest?} -> {path}; material.get {path} -> effective values + overrides[]; material.set {path,key,value} (dotted .rmat key; undoable) -> {path,key,dirty}; material.revert {path,key} -> {reverted}; material.save {path}; material.preview {path, shape?: sphere|cube|plane, rotate?}; material.assign {node, path} -> {node,material}; material.list {} -> {editors:[{path,dirty}]}
shader.create {path, lit?=true} -> {path,fragment}; shader.compile {path} -> {ok, compiled, diagnostics:[{document,file,line,column,error,message}]}; shader.diagnostics {path}; shader.uniforms {path} -> {uniforms:[{name,type,default,min?,max?,hint?}]}; shader.reload {path}
shadergraph.open|create {path, master?: pbr|unlit}|node_types|get|add_node {path,type,id?,x?,y?}|remove_node {path,id}|connect {path,from,from_port,to,to_port}|disconnect {path,to,to_port}|set_param {path,id,key,value}|set_input {path,id,port,value|null}|validate -> {ok,problems:[{code,node,port,message}]}|glsl -> {fragment}|save -> {path, shader}

## Level look (0.9.0)
environment.get {} -> {present, environment|null, baked, needs_bake}
environment.set {key,value}|{values:{k:v}} (dotted: sky.source, sky.zenith [r,g,b], fog.enabled, fog.density, ibl.resolution, volumetric.steps, post_stack; or field names exposure, fog-density, sky-color) -> as get; undoable, creates the block
environment.reset {} ; environment.bake {} -> {started} (window only) ; environment.progress {} -> {baking, phase idle|starting|environment|probes|done|failed, progress, error, baked, probes} ; environment.show {}
post.effect_types {} -> {types:[{type, params:[{name,type,default,min?,max?}]}]}
post.open {path} -> {path,panel}; post.create {path, preset?: default|empty} -> {path}; post.get {path} -> {path,name,quality,hdr,dirty,effects:[{index,type,enabled,weight,params}]}
post.add {path,type,index?} -> {index}; post.remove {path,index}; post.move {path,from,to}; post.set {path,index,key,value} (key enabled|weight|param); post.save {path}
post.preview {path?, enabled} -> {preview}; post.save_look {path, viewport?} -> {path, effects}; post.assign {path} -> {post_stack}
lod.get {node} -> {node, asset, available, levels:[{level,triangles,screen_size,error}], screen_size?, current, forced}; lod.force {node, level (-1 auto)}
view.layers {viewport?, mask? (n|"all"|"none"|"0x.."), layer? (0..31|name), enabled?} -> {viewport, mask, layers:[{index,name,enabled}]}
view.render_mode {viewport?, mode?} -> {viewport, mode, modes[]}; modes lit unlit wireframe normals lighting-only overdraw uv light-complexity ao reflections
Actions: environment.show/add/remove/bake, post.save-look, inspector.show

## Lighting bake (0.9.0)
bake.get {} -> {settings{key:value}, set_keys, lightmap|null, probes[], baking, phase, progress, auto_save, preview, result{present,unsaved,objects,lights,texels,entries,pages,grids,probes,seconds}}
bake.set {key,value}|{values:{k:v}}|{reset:true}|{auto_save} -> as get (one undo step; keys texels_per_unit samples bounces max_lightmap_size padding denoise probe_spacing probe_samples auto_probe_volume seed threads light_radius sun_angle)
bake.start {save?} -> {started,objects,lights} (async, CPU, headless ok) ; bake.progress {} -> {baking, phase idle|prepare|lightmaps|postprocess|probes|done|cancelled|failed, progress, error, unsaved} ; bake.cancel {} -> {cancelled}
bake.save {dir?} -> as get (files next to the level + refs, one undo step) ; bake.clear {} -> {cleared} ; bake.evaluate {position?,normal?} -> {position,normal,rgb,luminance} (no position: viewport centre) ; bake.preview {show?,page?} -> {show,page,pages} ; bake.show {}
Actions: bake.show/start/cancel/save/clear/toggle-preview. Node kind probe-volume (extents_x/y/z, spacing, resolution).

## Sequence methods (0.9.0, Sequencer panel)
path = .rseq (abs or project relative; opened when not the open one; omitted = open sequence). track = index from sequence.get. Errors: invalid-params / not-found / failed. Every edit is one step of the panel's own undo.
- sequence.list {} -> {open, dirty, tracks}; sequence.open {path} -> {path, panel}; sequence.create {path, name?, length?=5, fps?=30, play_mode?} -> {path} (existing file = failed)
- sequence.get {path?} -> {path,name,length,fps,play_mode,dirty,time,playing,bindings:[{guid,name}],tracks:[{index,kind,name,binding,binding_name,muted,bone?,channels?:{ch:[key]},component?,property?,keys?,sections?:[{index,start,duration,end,name,payload?,offset,rate,loop,weight}]}]}; key={index,time,value,tangent,in,out}
- sequence.set {path?, name?, length?, fps?, play_mode?}; sequence.add_track {kind, name?, binding?, bone?, component?, property?} -> {index}
- sequence.add_key {track, channel?, time, value?, tangent?, slope? | in?/out?} -> {index}; sequence.set_key {track, channel?, key | at, time?, value?, tangent?, slope?/in?/out?} -> {index}
- sequence.add_section {track, start, name, duration?, payload?, offset?, rate?, loop?, weight?/volume?} -> {index}; sequence.remove {track [+ section | + channel? + key/at | + channel] | binding} -> {removed}; sequence.bind {node, name?, track?} -> {guid, name}
- sequence.play {playing?=true, stop?, mode?: once|loop|ping-pong, pilot?} -> {time, frame, playing, mode, camera, events}; sequence.scrub {time | frame} -> {time, frame, playing, mode, camera, events:[{name,payload,time,reverse}]}
- sequence.save {path?, save_as?} -> {path}; sequence.undo {redo?} -> {ok, dirty}; sequence.from_timeline {timeline, path, fps?} -> {path}
- timeline.tangent {name?, node, time, channel?, tangent: eased|auto|user|broken|linear|constant, slope? | in?/out?} -> {changed} (level undo step)

## 2D skeletons (0.9.0)
Registered by the 2D rig editor (assets panel at startup); full table in docs/editor/ipc-protocol.org#ipc-skel2d. Every method takes {path} (.rskel2d, project-relative ok; opens the tab). Bones/slots by name. Each mutation = one undo step of the rig document.
skel2d.create {path, name?} -> {path}; skel2d.open {path} -> {path,panel}; skel2d.save {path}
skel2d.get {path} -> {name, dirty, mode, tool, skin, clip, time, playing, loop, selected:{bone,slot,attachment}, bones:[{name,parent,x,y,rotation,scale:[x,y],length}], slots:[{name,bone,attachment,color}], draw_order:[], skins:[{name,attachments:[{slot,name,image,vertices,triangles,weighted}]}], clips:[{name,duration,loop,bones,slots}]}
skel2d.add_bone {name?, parent?, x?, y?, rotation?, length?} -> {bone,index}; skel2d.set_bone {bone, x?,y?,rotation?,scale_x?,scale_y?,length?, name?}; skel2d.remove_bone {bone} -> {removed:[...]}
skel2d.add_slot {name?, bone, attachment?} -> {slot,index}; skel2d.set_slot {slot, attachment? (null hides), color? [r,g,b(,a)], name?}; skel2d.remove_slot {slot}; skel2d.draw_order {order:[slot...]} back to front
skel2d.attach {slot, name?, image?, skin?, x?, y?, width?, height?, columns?=1, rows?=1} -> {slot,name,vertices,triangles,weighted}; skel2d.detach {slot,name,skin?}
skel2d.mesh {slot,name,skin?, add?:[[x,y]], move?:{index,x,y}, remove?:index, triangulate?:true}; skel2d.auto_weights {slot,name,skin?, bones?, falloff?=4, max_influences?=2, smooth?=1}; skel2d.paint_weights {slot,name,skin?, bone, x, y, radius?=20, strength?=0.5, erase?} -> {changed}
skel2d.skin {name, op?: activate|add|remove} -> {skins,active}; skel2d.clip {name, op?: select|add|remove|set, duration?, loop?} -> {clips,clip}
skel2d.key {target, channel: translate|rotate|scale|color|attachment, time, clip?, values?, attachment? (null hides), curve?: "linear"|"stepped"|[x1,y1,x2,y2], remove?} -> {target,channel,keys:[{time,values|attachment,curve}]}
skel2d.play {clip?, time?, playing?, loop?} -> {clip,time,playing,loop}; skel2d.mode {mode?: setup|animate, tool?: select|bone|mesh|weights, bone?, slot?} -> {mode,tool,selected}; skel2d.pick {x,y,radius?=8} -> {bone|null}

## Animation (0.9.0)
Registered at startup by the assets panel. `path` is a .ranim/.rbonemap (opens the tab); `model` is a .glb/.gltf; every edit is one undo step of that file's document. Full tables: docs/editor/ipc-protocol.org#ipc-animation.
anim.open {path} -> {path,panel}; anim.create {path, model?} -> {path}
anim.get {path, machine?} -> {name, machine, breadcrumbs, layers[], states:[{name,kind,clip?,default,x,y,active}], transitions:[{from ("*"=any),to,duration,exit_time?,offset,priority,interruption,conditions}], parameters:[{name,type,default?,value}], clips:[{name,source,animation,missing}], preview}
anim.enter {path, layer?|state?|up?} -> {machine, breadcrumbs}; anim.add_layer|remove_layer {path,name}
anim.add_state {path,name,kind?,clip?,x?,y?}; anim.remove_state {path,name}; anim.set_state {path,name,rename?,clip?,speed?,speed_parameter?,mirror?,default?,x?,y?}
anim.add_transition|set_transition {path,from,to,duration?,exit_time? (null clears),offset?,priority?,interruption?,can_transition_to_self?,conditions?}; anim.remove_transition {path,from,to}
anim.add_parameter {path,name,type,default?}; anim.remove_parameter {path,name}; anim.set_parameter {path,name,value} (live only)
anim.set_blend_tree {path,state,type?,parameter?,parameter_y?,sync?,children?}
anim.preview {path,model?,restart?,play?,advance?,scrub?} -> {model,playing,time,states:{layer:state},transition|null}; anim.validate {path} -> {ok,issues[]}; anim.save {path}
bonemap.open|create {path,source?,target?,auto_fill?} -> {path,added}; bonemap.get {path} -> {rules,pairs,source_model,target_model,clip,mapping:[{target,source|null,explicit}],unmapped_target,unmapped_source}
bonemap.set {path,source,target|null}; bonemap.auto_fill {path} -> {added}; bonemap.set_rules {path,ignore_case?,normalize_separators?,normalize_sides?,strip_prefixes?}; bonemap.set_models {path,source?,target?,clip?}; bonemap.preview {path,clip?,time?,play?}; bonemap.save {path}
clip.list {model} -> {clips:[{name,duration,tracks,loop}],bones,morph_targets:[{name,group,weight}]}; clip.info {model,clip} -> {tracks:[{bone,channels:[{kind,interpolation,keys}]}],events}; clip.preview {model,clip?,time?,play?}; clip.set_morph {model,target,weight}
