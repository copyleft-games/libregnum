# EDTERR IPC methods -- FINAL

Common: `node` = node GUID, path or name (lrg_editor_find_node); default for terrain.* is the
terrain node the Terrain panel shows (primary selection when it is a `terrain` node). Errors:
INVALID_PARAMS (missing/bad params, no node selected), NOT_FOUND (no such node, not a
terrain/water node, asset does not load), FAILED (no terrain panel, engine save/load errors, limits).
Every mutating method is ONE undo step (level history for terrain.* and water.* with node;
the .rwater document's history for water.* with path) unless noted.
terrain.* exist once the Terrain panel is constructed (lrg_editor_terrain_panel_new); water.* are
registered by lrg_editor_water_editor_install (the Terrain panel, the first .rwater editor and, with the m67e-patches/lrg-editor-assets-panel.c.EDTERR.patch, the Assets panel call it).

## terrain.* (src/editor/ui/lrg-editor-terrain-panel.c)
- terrain.get {node?} -> {node (guid), name, asset (node's asset string), path (abs), dirty, resolution, chunk_size, cell_size, chunks:[x,z], cells:[x,z], samples:[x,z], size:[w,d], height_range:[min,max], splat_mode:"weights"|"ids", holes, lod_levels, bake_resolution, layers:[{name,texture|null,material|null,tiling,color:[r,g,b,a]}], foliage:[{name,kind:"mesh"|"billboard"|"grass",mesh|null,texture|null,color:[4],density,scale:[2],slope:[2],height:[2],align,size:[2],fade:[2],seed,instances}], brush:{tool,radius,strength,falloff,target_height,sample_target,layer,foliage_layer,noise_scale,seed}}
- terrain.brush {tool?, radius?, strength?, falloff?, target_height?, sample_target?, noise_scale?, seed?, layer? (surface layer index painted), foliage_layer?} -> the brush object as in terrain.get. Not undoable (panel settings). tool nicks: select raise lower flatten smooth noise paint hole fill foliage-paint foliage-erase; falloff nicks: linear smooth sphere sharp constant curve.
- terrain.stroke {node?, points:[[x,z],...] (also [x,y,z]), local?:bool=false (false: world x,z dropped straight down onto the terrain; points off the terrain are skipped), tool? (default the picked tool; must not be select), amount?=1 (per dab), invert?:bool (Ctrl: raise<->lower, hole<->fill, foliage-paint<->foliage-erase), smooth?:bool (Shift: smooth instead), + any terrain.brush field as a one-stroke override} -> {changed:bool, dabs, tool, label?}. One undo step labelled "Terrain: <Op> stroke". Errors: INVALID_PARAMS no points / no tool / unknown tool / no point on the terrain.
- terrain.sample {node?, x, z, local?:bool=false} -> {hit, height?, normal?:[3], weights?:[per layer], local?:[3], world?:[3], hole?, foliage_density?:[per foliage layer]}
- terrain.add_layer {node?, name?, texture?, material?, tiling?, color?:[r,g,b(,a)]} -> {index}  (FAILED at 8 layers)
- terrain.remove_layer {node?, index} -> {removed:true}  (undo restores the layer AND its weights; FAILED for the last layer)
- terrain.set_layer {node?, index, name?, texture?, material?, tiling?, color?} -> {index}
- terrain.add_foliage {node?, name?, kind?="mesh"|"billboard"|"grass", + set_foliage fields} -> {index}
- terrain.remove_foliage {node?, index} -> {removed:true}  (undo restores layer + density map)
- terrain.set_foliage {node?, index, mesh?, lod_mesh?, lod_distance?, texture?, color?:[3|4], density?, scale?:[min,max], slope?:[min,max] (degrees), height?:[min,max], align?, size?:[w,h], fade?:[start,end], seed?} -> {index}
- terrain.rebake {node?} -> {instances}  (re-scatter foliage, drop chunk meshes / splat bakes; NOT undoable)
- terrain.save {node?} -> {path}  (writes .rterrain + sidecars; NOT undoable; dirty terrains are also written when the level is saved)
- terrain.create {kind?="terrain"|"water", name?} -> {guid, name, asset}  (writes <project>/terrain/<name>.rterrain (4x4 chunks of 32 cells, 1 m cells, layers grass+rock; node placed at camera target - (64,0,64)) or .rwater (a 4-point lake, one wave) and adds the node: one undo step for the node; FAILED without a level)
- terrain.show {} -> {panel:"terrain", shown}

## water.* (src/editor/ui/data/lrg-editor-water-editor.c)
Each takes EITHER {path} (a .rwater file: opened in its data editor when needed; edits are document undo steps) OR {node} (a level water node: edits the shared store body, level undo step, file marked dirty and written on level save; an open editor of the same file follows).
- water.get {path|node} -> {path, node?, name, shape:"plane"|"lake"|"river", level, size:[w,l], points:[[x,y,z,width],...], cell_size, foam:[band,depth], waves:[{direction:[x,z],amplitude,wavelength,speed,steepness}], wave_scale, colors:{shallow:[4],deep:[4],foam:[4]} (0..1), optics:{transparency,reflection,refraction,reflect_distance}, dirty}
- water.set {path|node, name?, shape?, level?, size?:[w,l], cell_size?, wave_scale?, foam?:[band,depth], transparency?, reflection?, refraction?, reflect_distance?, colors?:{shallow?,deep?,foam?:[r,g,b(,a)]}} -> as water.get
- water.set_wave {path|node, count?:0..4, index?:0..3, direction?:[x,z], amplitude?, wavelength?, speed?, steepness?} -> as water.get  (needs index and/or count)
- water.add_point {path|node, x, z, y?=0, width?=4, index? (insert before; default append)} -> {index}
- water.set_point {path|node, index, x?, y?, z?, width?} -> {index}
- water.remove_point {path|node, index} -> {removed:true}
- water.save {path|node} -> {path}

## Actions (lrg_editor_actions; run with action.run)
terrain.show, terrain.create (Create > Terrain), terrain.create-water (Create > Water), terrain.save,
terrain.rebake, terrain.tool-<nick> for every tool (select raise lower flatten smooth noise paint hole
fill foliage-paint foliage-erase; checked = the picked tool).
