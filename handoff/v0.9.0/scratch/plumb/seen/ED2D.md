# ED2D IPC methods (FINAL)

Docs: docs/editor/2d-tools.org (#editor-2d-ipc has the same tables with more words).

Common: `node` = node name/path/guid like other node methods (default: the tiles target, i.e. the
primary selection when it is a tilemap node). `path` = an .rtileset / .rsprite file, absolute or
relative to the project (default: the focused editor of that kind, else the first open one; with a
path the tab is opened if needed). Tile references follow the ENGINE convention of the place: map
cells / brush values are 1-based ids with transform bits (0 = empty), tileset entries are 0-based
tile indices. Errors: invalid-params for missing/bad params, not-found for no target / no such
tile, rule tile, rule, output, clip, frame or editor, failed for engine errors (load, save,
schema). Mutating `tiles.*` methods are ONE undo step in the level history; mutating `tileset.*` /
`sprite.*` methods are ONE undo step in the document's own history (`data.undo`).

## tiles.* (registered by src/editor/ui/lrg-editor-tiles-panel.c)
- tiles.get {node?} -> {node, tileset, tileset_kind:"none"|"image"|"rtileset", size:[w,h], tile_size:[w,h], grid:{orientation:"orthogonal"|"isometric"|"staggered"|"hexagonal", stagger_axis:"x"|"y", stagger_index:"odd"|"even", hex_side, cell_size:[w,h]}|null, layout:"square"|"staggered-odd"|...|"hex-rows-odd"|... (rule tile neighbour layout nick), layers, brush:{tile, id, mirrored, rotation, rule_tile|null}, rule_tiles:[{name, default, n_rules}], tile:{...tiles.tile_info of the brush tile}}
- tiles.grid {node?, orientation?, stagger_axis?, stagger_index?, hex_side?, cell_size?:[w,h]} -> tiles.get   (one undo step; the node needs a grid: tilemap.configure first)
- tiles.brush {tile?:id, rule_tile?:name|null, mirrored?:bool, rotation?:0..3, rotate?:quarter turns (+cw/-ccw), flip_x?:bool, flip_y?:bool} -> {tile, id, mirrored, rotation, rule_tile}   (not undoable; X / Shift+X / Z keys do flip_x / flip_y / rotate 1 while painting)
- tiles.paint {node?, cells:[x,y,...], rule_tile?:name, layer?:index, erase?:bool} -> {changed}   (with a rule tile (param or brush) the cells and their neighbours are autotiled; else the brush value with its bits is written; one undo step; creates the grid when missing)
- tiles.refresh {node?, layer?:index (default all)} -> {changed}   (Refresh all; one undo step; needs an .rtileset)
- tiles.cell_at {node?, local:[x,y]} -> {cell:[x,y], inside:bool, polygon:[[x,y],...] (local units, 4 or 6 points)}
- tiles.tile_info {node?, tile:id} -> {tile, index, flags:[solid|animated|hazard], collision:[[x0,y0,...],...], data:{k:v}, animation:[{tile,duration}], rule_tile|null}

## tileset.* (registered by src/editor/ui/data/lrg-editor-tileset-editor.c)
- tileset.create {path, image?, tile_size?:[w,h]=[16,16], name?} -> {path}   (writes a new .rtileset; columns/tile_count from the image size, 8x8 without one; refuses to overwrite)
- tileset.open {path} -> {path, panel}
- tileset.get {path?} -> {path, dirty, selected, selected_rule_tile, selected_rule, issues:[msg], error?, name, image, tile_size, columns, tile_count, margin, spacing, offset, tiles:[{id, flags, collision, data, animation}] (only tiles with something), rule_tiles:[{name, default, seed, border_is_this, match_tiles, rules:[{pattern:[3 rows of '#','-','.','@'], transform, tiles:[{tile, weight, rotate, flip}]}]}]}
- tileset.set {path?, name?, image?, tile_size?, columns?, tile_count?, margin?, spacing?, offset?:[x,y]} -> tileset.get
- tileset.select {path?, tile?, rule_tile?, rule?, page?:"tile"|"rules"} -> {selected, selected_rule_tile, selected_rule}   (not an edit)
- tileset.tile {path?, tile:index, flags?:[solid|animated|hazard], data?:{key:value|null}, animation?:[{tile, duration}]|[]} -> tile entry {id, flags, collision, data, animation}
- tileset.collision {path?, tile, op:"add"|"clear"|"remove"|"move_point"|"insert_point"|"remove_point", polygon?:[x0,y0,...] (add), index? (polygon), point?, x?, y?} -> tile entry
- tileset.rule_tile {path?, name, op:"add"|"preset"|"remove"|"rename"|"set", preset?:"edges16"|"blob47", first?:index, new_name?, default?:index|{tile,rotate,flip}|null, seed?, border_is_this?} -> rule tile entry ({removed:true} for remove)
- tileset.rule {path?, rule_tile, op:"add"|"remove"|"move"|"set", index?, to? (move), pattern?:[3 rows], neighbor?:"n".."nw"|"all" + match?:"any"|"this"|"not-this", transform?:"fixed"|"rotated"|"mirror-x"|"mirror-y"|"mirror-xy", tiles?:[index|{tile, weight?, rotate?, flip?}] (add: the outputs; set: replaces them)} -> rule tile entry
- tileset.preview {path?, rule_tile, cells:[x,y,...], size?:[w,h]=[8,8]} -> {size, tiles:[cell values]}   (resolves a scratch map with square neighbours; no edit)
- tileset.save {path?} -> {path}

## sprite.* (registered by src/editor/ui/data/lrg-editor-sprite-editor.c)
- sprite.create {path, image?, name?, frame_size?:[w,h]} -> {path}   (writes a new .rsprite; with frame_size the image is grid-sliced; refuses to overwrite)
- sprite.open {path} -> {path, panel}
- sprite.get {path?} -> {path, dirty, selected_frame, selected_clip, playing, time, preview_frame, preview_position, issues, error?, name, image, image_size, grid:{frame_size, columns, count}|null, frames:[{index, name, rect:[x,y,w,h], pivot:[x,y], grid:bool}], clips:[{name, fps, loop, frames:[{frame, duration}], events:[{frame, name}], duration}]}
- sprite.slice {path?, mode:"grid"|"alpha"; grid: frame_size, columns?, rows?, count?, margin?:[x,y], spacing?:[x,y], pivot?:[x,y]; alpha: threshold?=0, min_size?=2, pivot?:[fx,fy]=[0.5,1]} -> sprite.get
- sprite.frame {path?, op:"add"|"set"|"remove"|"select", index?, rect?:[x,y,w,h], name?, pivot?:[x,y]} -> frame entry ({removed:true} for remove; grid frames only take name/pivot)
- sprite.clip {path?, op:"add"|"remove"|"set"|"select", name, new_name?, fps?, loop?:"once"|"loop"|"ping-pong", frames?:[index|name|{frame,duration}] (set: replaces the list)} -> clip entry
- sprite.clip_frame {path?, clip, op:"add"|"insert"|"remove"|"move"|"set", index?, to?, frame?:index|name, duration?} -> clip entry
- sprite.event {path?, clip, op:"add"|"remove", frame:clip position, name?} -> clip entry
- sprite.play {path?, clip?, playing?:bool, time?:seconds} -> {clip, playing, time, frame, position}   (the preview; not an edit)
- sprite.save {path?} -> {path}
