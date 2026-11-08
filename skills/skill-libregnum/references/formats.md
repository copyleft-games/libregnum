# Data formats (YAML)

## Composite documents (LrgDataLoader, 0.9.0): base of .rmat/.ranim/.rbt/.rnav/.rparticles/.rterrain/.rmixer

- Load: `lrg_data_loader_load_composite_file (loader, path, ROOT_TYPE or G_TYPE_INVALID, &err)`; `_data (text, len, root_type, source, &err)`. Save: `lrg_data_loader_save_to_file` / `_to_data`.
- Object property (GObject class/interface) = nested mapping; `type: <registry name>` is optional when the property type is concrete.
- GListModel property, or GPtrArray with `lrg_data_loader_set_param_item_type (pspec, T)` in class_init = sequence of mappings.
- LrgDataContainer objects take `children:` (loader property `children-key`); `get_child_type()` = default child type.
- `{ref: x}` anywhere an object goes, resolved after the build (forward refs OK): `#` root, `#/a/0` JSON pointer, a unique guid/id in the document, else the resolver (`lrg_data_loader_set_resolver`). Resolved objects are tagged (`lrg_data_loader_get_object_ref`) and saved back as `ref:`.
- Strict by default (`lrg_data_loader_set_strict`): one GError lists every problem as `file:line:col: /path: message`.
- Reserved keys: `type`, `ref`, `children` (on containers). Saving writes `type:` only where not implied (root always), skips values equal to a fresh `g_object_new (type)`, writes floats in shortest exact form; output is byte-stable on reload.
- Traps: GPtrArray without an item-type hint is ignored; duplicate ids make ref-by-id ambiguous; construct-only props cannot hold refs/lists; types written with `type:` must be registered.

## Engine formats with writers (all round-trip byte-stable)

| Format | Load | Save | Notes |
|---|---|---|---|
| locale | `lrg_locale_new_from_file` / `_from_data` | `lrg_locale_save_to_file` / `_data` | code, name, plural_rule (nick, optional), strings (text or plural-form map); a nested map is always plural forms |
| dialog tree | `lrg_dialog_tree_new_from_file` / `_from_data` | `lrg_dialog_tree_save_to_file` / `_data` | id, title, description, start, nodes; a node without next/responses auto-chains, `next: null` = terminal |
| sound bank | `lrg_sound_bank_new_from_file` / `_from_data (…, base_dir, manifest_only)` | `lrg_sound_bank_save_to_file` / `_data` | name, base_path, volume, sounds; manifest_only = no audio device/files needed |
| input map | `lrg_input_map_load_from_file` / `_from_data` | `lrg_input_map_save_to_file` / `_data` | actions keep order; unknown root/action/binding keys kept |
| HUD layout | `lrg_hud_layout_load_from_file` / `_from_data` | `lrg_hud_layout_save_to_file` / `_data` | version, pieces (sorted) |

Editor data editors validate with these from-data loaders (no temp files) but save through the document model (keeps comments). Guide: docs/guides/writing-a-data-format.org.

## Post stacks (.rpost, 0.9.0): LrgPostStack
- `version: 1`, `name`, `quality: low|medium|high|ultra`, `hdr: true`, `effects:` (ordered list). Entry: `type:` (bloom, tonemap, vignette, color-grade, fxaa, film-grain, screen-shake, heat-haze, colorblind-filter, sharpen, palettize, or a registered/GType name), `enabled`, `weight` (0..1 = base intensity), `priority` (default index*10), `params:` = effect GObject properties (kebab or snake case).
- `[r, g, b]` expands to `name-r/g/b` (bloom `tint`, vignette `color`, color-grade `lift`/`gamma`/`gain`). Special: palettize `palette: ["#rrggbb", ...]`; color-grade `lut: file.cube` (relative to the .rpost) + `lut_strength`.
- Load `lrg_post_stack_new_from_file` / `_from_data (text, len, base_dir, &err)`; save `lrg_post_stack_save_to_file` / `_to_data` (byte-stable; unknown top-level and per-effect keys kept). Strict: unknown effect type / param, bad value, version > 1 are errors (LRG_POST_STACK_ERROR_*). Not an LrgDataLoader document (own reader: per-effect param validation).
- `lrg_post_stack_build (stack, processor, &err)` reuses same-type effects in order, resets unset params to defaults (except screen-shake trauma), removes unlisted effects: cheap per frame. `lrg_post_stack_blend (a, b, t)` for post volumes. Custom effects: `lrg_post_stack_register_effect_type ("my-sepia", TYPE)`.
- Order: bloom (wants HDR) -> tonemap -> grade/colorblind/sharpen/fxaa/palettize/film-grain/vignette. Docs: docs/modules/postprocess/post-stack.org.

## Model `.meta` LOD keys (0.9.0)
- Under `settings:` of `importer: model`: `lod-levels` (1..8, 1 = none), `lod-ratios` (list for levels 1.., default 0.5^i), `lod-screen-sizes` (list for levels 0.., default 0.5^(i+1) with last 0; a positive last value culls), `lod-max-error` (0.05), `lod-lock-border` (true), `lod-hysteresis` (0.1), `lod-fade-duration` (0.25 s).
- Lists may be `"0.5, 0.25"` or `[0.5, 0.25]`; `asset.meta.set` / `lrgeditorctl meta set` validate them.
- `lrg_asset_manager_load_model` ignores these keys; `LrgLodComponent` and `lrg_lod_group_new_from_model_with_meta` read them.

## Shader assets (.rshader) and shader graphs (.rshadergraph), 0.9.0
- `.rshader` (LrgShaderAsset): `version: 1`, `name`, `lit: true` (prepends lrg_light_set_get_glsl_library(): lrg_light_pbr/lrg_surface/lrg_light_shade, lrg_to_linear/lrg_to_display, mapCamera), `vertex:`/`fragment:` = a one-line file path (relative to the .rshader) or a multi-line `|` block of GLSL 330 *body* (no #version), `render_state: {blend: opaque|cutout|alpha|additive|multiply, cull: back|front|none, depth_test, depth_write, alpha_cutoff}`, `uniforms: {name: {type: float|vec2|vec3|vec4|color|int|bool|sampler2D, default, range: [min,max], hint}}`. Unknown top-level keys kept; unknown uniform keys are errors.
- Contract: the engine prepends #version 330 + declarations (vertexPosition.., mvp/matModel/matView/matNormal, fragPosition/fragTexCoord/fragColor/fragNormal, finalColor, texture0, colDiffuse, lrgTime, lrg_view_position(), the described uniforms); anything you declare yourself is skipped. Lit stages must NOT redeclare library uniforms (error with your line).
- API: `lrg_shader_asset_new_from_file/_from_data (text, len, base_dir, &err)`, `_save_to_file/_data`, `_compile` (needs a window; GError lists `file:line:col: error: msg` in YOUR line numbers; keeps the last good program), `_get_shader` (GrlShader, re-fetch after `reloaded`), `_set_uniform_*`, `_apply (asset, &unit)`, `_bind_lights (asset, light_set)` every frame for lit assets (else nothing draws), `_set_watch (asset, TRUE, &err)` hot reload (pump g_main_context_iteration each frame; `reloaded` / `reload-failed`), `_release` before CloseWindow.
- `.rshadergraph` (LrgShaderGraph, no UI dependency): `nodes: [{id, type, position: [x,y], params: {...}, inputs: {port: number|[..]}}]`, `links: [{from: node.port, to: node.port}]`. Node types: uv position normal view_dir time vertex_color parameter constant | add sub mul div lerp dot cross normalize pow clamp saturate one_minus fresnel step smoothstep sin cos abs fract floor min max split combine swizzle | texture_sample noise | pbr_output unlit_output (exactly one output).
- Graph API: `lrg_shader_graph_add_node/connect/set_node_param/set_input_value`, `_validate` (problems with node ids: cycle, type mismatch, missing input, outputs, params), `_generate_fragment` (deterministic GLSL), `_generate` -> LrgShaderAsset (pbr_output = lit). Types: float splats, wider vectors truncate, widening/sampler mixes are mismatches; dynamic ports take the widest input.
- Traps: lit GLSL works in linear (lrg_to_linear/lrg_to_display); a one-line stage value is a path; swizzling a float is not GLSL 330.

## Materials (.rmat, 0.9.0): LrgMaterial
- Load: `lrg_asset_manager_load_material (assets, "materials/crate.rmat" | "guid:…", &err)` (cached by canonical path, transfer none; headless) or `lrg_material_new_from_file (path, manager_or_NULL, &err)`. Save: `lrg_material_save_to_file()` (only own values; unknown top-level keys kept; byte-stable round trip).
- Keys (all optional): version, name, parent (path relative to the file, or guid:), shading_model lit|unlit|custom, shader (custom: .rshader ref), base_color [r,g,b,a] sRGB, metallic, roughness, occlusion_strength, normal_scale, emission_color [r,g,b], emission_strength, alpha_cutoff, height_scale, textures {albedo, normal, orm, emission, height, metallic_gloss: path|guid:|null}, render_state {blend opaque|cutout|alpha|additive|multiply, cull back|front|none, depth_test, depth_write, double_sided}, render_queue auto|opaque|cutout|transparent, uniforms {name: {type: float|vec2|vec3|vec4|color|int|texture, value: …} or shorthand number/list}.
- Instances: `lrg_material_new_instance (parent)` or `parent:`; getters return own -> inherited -> default; `lrg_material_is_overridden (m, "roughness" | "textures.albedo" | "uniforms.k")`, `lrg_material_revert()`. A parent loop fails with LRG_MATERIAL_ERROR_CYCLE; guid: needs an asset manager.
- Binding: static batch / terrain `set_material (id, m)`, `lrg_skinned_mesh_set_material`, mesh-renderer and skinned-model components `material` property + `set_submesh_material (i, m)`. Custom shading: `lrg_shader_asset_bind_lights` (lit) -> `lrg_shader_asset_apply (asset, NULL)` -> `lrg_material_apply (mat, lrg_shader_asset_get_shader_id (asset))` -> draw.
- Traps: call `lrg_material_reset_uniforms (shader_id)` after material draws; `apply_state`/`restore_state` flush rlgl (state first, then bind your program/VAO); colours are sRGB; materials write `emissionOwn`, never `emissionColor`; custom texture uniforms use the shared budget from unit 7 (unlit programs: 7..GL_MAX-1; lit programs: 7..10 only on >16-unit drivers, none on 16-unit ones); a sampler already on a budget unit (shader asset) keeps it; over budget = warning, not bound.

## HDR images and environments (0.9.0)
- .hdr (Radiance RGBE): LrgHdrImage (graphics/lrg-hdr-image.h) reads flat/old-RLE/new-RLE scanlines, -Y and +Y orientations, EXPOSURE divided out; writes "-Y H +X W" with new RLE. Linear float RGB. raylib's HDR loader is disabled - always use this. Equirect mapping: u = 0.5 + atan2(x,-z)/2pi (centre = -Z), v = acos(y)/pi.
- environment: block in .rlevel -> LrgEnvironment (see level-format.md). lrg_environment_load_from_data/to_data also read/write a bare block or a document with an environment: member. Diffuse IBL is L2 SH (uniforms, no texture unit); specular is a GGX-prefiltered cubemap; BRDF LUT on the CPU (analytic fit when no unit is free).
- Traps: environment bake / probe capture / volumetric fog need a GL context, outside BeginDrawing/BeginTextureMode; release_gpu before CloseWindow. Never rlUnloadTexture() a renderbuffer id (rlLoadTextureDepth(..., true)): it deletes the texture with the same number (raylib's default white texture). Let rlUnloadFramebuffer free the depth attachment. Reflection-probe draw callbacks must not call BeginMode3D.

## Bone maps (.rbonemap, 0.9.0): LrgBoneMap
- Load `lrg_bone_map_new_from_file/_data`; save `lrg_bone_map_save_to_file` / `lrg_bone_map_to_data` (byte-stable, pairs sorted).
- Keys: `version: 1`, `rules: {ignore_case, normalize_separators, normalize_sides, strip_prefixes: [..]}` (booleans default true), `bones: {source_bone: target_bone}` (explicit pairs win over rules; one source per target). Errors in `LRG_RETARGET_ERROR` (PARSE, VERSION, IO).

## Sequences (.rseq, 0.9.0): LrgSequence
- Header `type: sequence`, `version: 1`, `name`, `length`, `fps`, `play_mode` (once|loop|ping-pong), `bindings: [{guid, name}]`, `tracks: [...]`; every track has `kind`, `name`, `binding`, `muted`.
- Kinds: `transform` (optional `bone`, `channels: {location.x: [keys], ...}`), `property` (`component` GType name, `property`, `keys`), `camera-cut` (`sections: [{start, camera}]`), `audio` (`sections: [{start, duration, sound: "bank:sound"|path, volume}]`), `event` (`events: [{time, name, payload}]`), `animation` (`sections: [{start, duration, clip, offset, rate, loop, weight}]`), `subsequence` (`sections: [{start, duration, sequence: name|relative.rseq, offset, rate, loop}]`, depth <= 8).
- Keys `{time, value, tangent: auto|user|broken|linear|constant, slope | in/out}`; auto is the default and omitted. Byte-stable writer; errors LRG_SEQUENCE_ERROR_{IO,PARSE,INVALID,VERSION}.

## Baked lighting files (0.9.0)
- `.rlightmap`: version, intensity, directional: false, pages: [*.hdr relative], entries: {guid: {page, scale_offset: [su,sv,ou,ov]}}, settings: {...}; atlas_uv = uv2*so.xy + so.zw.
- `.rprobes`: version, bounds {min,max}, resolution [nx,ny,nz], intensity, encoding: base64-f32le, sh: base64 (27 floats per probe, probe order x + nx(y + ny z)), valid: base64 (one byte per probe).

## 2D skeletons (.rskel2d, 0.9.0): LrgSkeleton2D
- Load `lrg_skeleton2d_new_from_file` / `_from_data`; save `lrg_skeleton2d_save_to_file` / `_data` (byte stable, defaults omitted). Our own format (Spine/DragonBones not supported).
- Root: `rskel2d: 1` (required), name, bones [{name, parent, x, y, rotation, scale: [sx,sy], length}] parents first, slots [{name, bone, attachment, color: [r,g,b,a]}] = setup draw order back to front, skins [{name, attachments: [{slot, name, image, region: [x,y,w,h], vertices: [[x,y,u,v]], weights: [[bone, w, ...]] one row per vertex (max 4 pairs, [] = follows slot bone), triangles: [[a,b,c]]}]}], animations [{name, duration, loop, bones: [{bone, translate: [{time,x,y}], rotate: [{time,angle}], scale: [{time,x,y}]}], slots: [{slot, attachment: [{time,name|null}], color: [{time,color}]}], draw_order: [{time, order: [all slots]|null}], events: [{time,name}]}].
- Keys: optional `curve: stepped | linear | [x1,y1,x2,y2]`. Bone values are offsets from setup (scale multiplies), colour absolute. y down, degrees clockwise, meshes in setup space.
- Strict errors LRG_SKELETON2D_ERROR_{PARSE,INVALID,VERSION,IO}, message `source: /path: msg`. Images resolve against the file's dir via lrg_skeleton2d_load_textures (needs a window).

## Animation graphs (.ranim, 0.9.0): LrgAnimGraph
- `version: 1`, `name`, `clips: {key: {source, animation}}`, `parameters: [{name, type: float|int|bool|trigger, default}]`, `root_motion: {bone, mode: none|xz|xyz, rotation, apply}`, `layers: [{name, weight, blend: override|additive, enabled, mask, mask_recursive, default, states: [{name, clip|blend_tree|machine, speed, speed_parameter, mirror}], any_state: [...], transitions: [{from, to, duration, exit_time, offset, interruption: none|source|destination|source_then_destination|destination_then_source, priority, can_transition_to_self, conditions: [{parameter, op, value}]}]}]`, `ik: [{name, type: two_bone|ccd|fabrik|look_at|foot, bones, effector, pole, target, axis, weight, iterations, tolerance, enabled, ray_up, ray_down, align}]`.
- `blend_tree: {type: 1d|2d_directional|2d_cartesian|direct, parameter, parameter_y, sync, children: [{clip|tree, threshold|position|parameter|weight, speed}]}`.
- Strict (errors carry the key path); unknown keys kept; canonical byte-stable writer. Load `lrg_anim_graph_new_from_file`; clips via `lrg_anim_graph_resolve_clips` (glTF) or `lrg_anim_graph_set_clip`.

## Physics assets (.rphysasset, 0.9.0): LrgPhysicsAsset
- Load `lrg_physics_asset_new_from_file` / `_from_data`; save `lrg_physics_asset_save_to_file` / `_data` (hand-written, strict, byte-stable, defaults omitted). Generate with `lrg_physics_asset_new_from_skeleton`.
- Root: `version: 1` (required), `name`, `exclude-adjacent` (default true), `parts: [..]`, `exclusions: [[boneA, boneB], ..]`.
- Part: `bone` (required, unique), `shape: capsule|box|sphere` (default capsule), `radius` (capsule/sphere), `half-height` (capsule), `half-extents: [x,y,z]` (box), `offset: [x,y,z]` and `rotation: [x,y,z,w]` in the bone frame (capsule axis = local Y), `mass` (kg), `joint:` { `kind: swing-twist|cone|hinge|point|fixed`, `position`, `rotation` (joint X = twist axis), `twist: [min,max]` (hinge, swing-twist), `swing: [y,z]` (cone, swing-twist), `friction`, `motor-max-torque` (300), `motor-frequency` (10), `motor-damping` (8) }. Keys that do not apply to the shape/joint kind are errors (`/parts/N/radius: does not apply to a box`).

## Fracture caches (.rfracture, 0.9.0): LrgFracture
- YAML: `format: rfracture`, `version: 1`, `source_hash` (SHA-256 of source geometry + build parameters), seed_mode, chunk_count, seed, interior_material, impact_radius, grid_jitter, impulse_threshold, density, optional impact_point, source_volume, source_bounds, seeds; per chunk: seed, volume, center_of_mass, inertia[9], bounds[6], neighbors, positions, normals, texcoords, indices, materials (one per triangle), hull. Floats with 9 significant digits (exact round trip). `lrg_fracture_load_or_build` rebuilds and rewrites when the hash differs.

## Mixer layouts (.rmixer, 0.9.0): LrgMixerAsset
- Load `lrg_mixer_asset_new_from_file` / `_from_data`; save `lrg_mixer_asset_save_to_file` / `_to_data` (strict, byte-stable, unknown keys kept). Apply: `lrg_mixer_asset_create_mixer (a, 48000, 2, &err)` or `lrg_mixer_asset_apply (a, mixer, &err)`; snapshots `lrg_mixer_asset_apply_snapshot (a, mixer, "name"|NULL, -1 /*own blend*/, &err)`, `lrg_mixer_asset_blend_snapshots`.
- Root: `version: 1`, `name`, `default_snapshot`, `buses`, `snapshots`, `ducking`. Bus: `name`, `parent` (default master), `volume` dB (-144..12.04, `-inf` ok), `mute`, `solo`, `sends: [{target, level}]`, `effects: [{type: low-pass|high-pass|echo|reverb|chorus|pitch-shift|<registered>, id (default type, unique per bus), enabled, params: {<property>: value}}]` (params = effect GObject properties, range-checked, applied in order; reverb `preset: hall` first). Snapshot: `name`, `blend` s, `volumes: {bus: dB}`, `sends: [{source, target, level}]`, `params: {bus/effect/param: number}`; unlisted values return to base. Ducking: `{trigger, target, threshold dB, depth dB <= 0, attack s, release s}`. Errors `LRG_MIXER_ASSET_ERROR_*` with `/buses/N/...` paths.
- Project default: `project.ryaml` `audio: { mixer: audio/main.rmixer }` (`lrg_project_get/set_audio_mixer`); runtime `lrg_audio_manager_load_project_mixer (lrg_audio_manager_get_default (), level_path, 48000, &err)`.

## Behaviour trees (.rbt) and logic state machines (.rfsm), 0.9.0
- .rbt: `version, name, blackboard: [{name, type: int|float|bool|string|object, default}], subtrees: {n: {path}|{root}}, root:`. Node: `type, id, name, children|child, params, <type keys>`. Types: sequence, selector, parallel(policy one|all), inverter, repeater(count, 0=forever), succeeder, failer, cooldown(seconds), time_limit(seconds), guard(key,op,value), action(action|script), condition(condition|script), check(key,op,value), set(key,value), wait(duration), subtree(ref); custom LrgBTNode GTypes via `lrg_bt_register_node_type`. op: eq ne lt le gt ge set unset. Quoted scalars are strings.
- .rfsm: the top level is the root state: `blackboard, initial, enter/update/exit: [actions], transitions: [any-state], states: [{name, initial, enter, update, exit, transitions, states}]`. Transition `{to, event, after, when: [conditions], do: [actions]}`; `to` = a name resolved outward, `a/b` or `/abs`. Actions `{action|script, params}`, `{set, value}`, `{event}`; conditions `{key, op, value}`, `{condition|script, params}`.
- Both: byte-stable after the first save, unknown keys kept (errors inside action/condition entries), strict errors with the key path.

## Tilesets (.rtileset) and sprite sheets (.rsprite), 0.9.0
- .rtileset: version, name, image, tile_size, columns, tile_count, margin, spacing, offset; `tiles: [{id, flags, collision: [polygons], data: {k: v}, animation: [{tile, duration ms}]}]`; `rule_tiles: [{name, default, seed, border_is_this, match_tiles, rules: [{pattern: ["#.#", "-@-", "..."], transform, tiles: [index | {tile, weight, rotate, flip}]}]}]`. Tile references in the file are 0-based (cells in code are 1-based).
- .rsprite: version, name, image, image_size; `grid: {frame_size, columns, rows, count, margin, spacing, pivot}`; `frames: [{index override (name/pivot) | name, rect, pivot}]`; `animations: [{name, fps, loop, frames: [index | name | {frame, duration}], events: [{frame, name}]}]`.
- Both: unknown keys kept, strict errors with key paths, byte-stable save.

## Terrain (.rterrain) and water (.rwater), 0.9.0
- .rterrain: `version, name, grid {resolution, chunk_size, chunks[2]}, heights {file, format: png16|r32, range[2]}, splat {mode: weights|ids, files [RGBA png, 4 layers each], ids_file}, layers [{name, texture, material, tiling, color}], holes {file}, lod {levels, distances[], skirt}, bake_resolution, foliage {seed, patch_cells, layers [{name, kind, mesh, lod_mesh, lod_distance, texture, color, density, material_density[], scale, slope, height, align, size, fade, seed, density_file}]}`. Sidecars sit next to the file named after its stem; unknown keys kept at every level.
- .rwater: `version, name, shape (plane|lake|river), level, size, points ([x,z] lakes, [x,y,z,w] rivers), cell_size, foam[2], waves [{direction, amplitude, wavelength, speed, steepness}], wave_scale, colors {shallow, deep, foam}, optics {transparency, reflection, refraction, reflect_distance}`; unknown keys kept at the root only.
- Both strict (`file: /key/path: problem`) and byte-stable.

## Particle effects (.rparticles), 0.9.0
- LrgParticleAsset (`particles/lrg-particle-asset.h`): `version: 1`, `name`, `seed`, `duration` (0 = endless), `loop`, `prewarm`, `local-space`, `emitters: [{name, max-particles, enabled, start-delay, renderer: {kind: none|billboard|ribbon|mesh, blend, layer, texture, columns/rows/fps, ribbon-mode trail|connect, width, width-curve, color-gradient, uv-mode stretch|tile|per-segment, max-points, min-distance, trail-lifetime, mesh, primitive, mesh-scale, align none|velocity|tumble, lit}, modules: [{type: <kind>, ...}]}]`.
- Module kinds: spawn-rate, burst, shape, velocity, lifetime, size, color, rotation, size/color/alpha/rotation-over-life, gravity, wind, drag, turbulence, vortex, attractor, collision (plane|world), sub-emitter (trigger birth|death|collision -> emitter name).
- Ranges are `n` or `[min, max]`; colours and gradients quoted (`'#rrggbb'`, `'0 #fff; 1 #0000'`); curves `"t v [easing]; ..."`.
- Load with `lrg_particle_asset_new_from_file/_data`, play with `lrg_particle_effect_new` or LrgParticleComponent (`asset-path`). Unknown keys kept and listed by `get_unknown_keys()`; errors read `file: /key/path: msg`; byte-stable round trip.

## Navigation bake settings (.rnav), 0.9.0: LrgNavBake
- `lrg_nav_bake_new_from_file/_from_data`, `save_to_file/_to_data`. Strict, byte-stable, unknown keys kept at the top level, in agent/voxel/regions/sources, and in each area and link.
- Keys: `version: 1, name, agent{radius 0.3, height 1.8, max_slope 50, max_step 0.45, max_drop 0}, voxel{cell_size 0.25, cell_height 0.1}, regions{min_area 0, corner_margin 0}, sources{meshes true, colliders true, static_only false, layer_mask, include_tags, exclude_tags, physics_layers, bounds{min,max}}, areas[{name, tag (nonzero, unique), cost > 0, boxes[{min,max}]}], links[{start, end, bidirectional true, cost 0, area}], output`.
- Errors are `LRG_NAV_BAKE_ERROR_*` with key paths such as `areas[0].boxes[0].max[2]`.


## Songs (.rsong), 0.10.0: LrgSong
- `lrg_song_new_from_file` / `_new_from_data`, `lrg_song_save_to_file` / `lrg_song_to_data`. Byte-stable writer (instruments and patterns sorted by id, tracks and markers in order, defaults left out, shortest round-tripping numbers). Errors `LRG_SONG_ERROR_*` name the YAML path (`patterns.bass-a.notes[3]: expected "beat key length [velocity]"`); `lrg_song_validate()` checks instruments, patterns, files, buses, effect ids, parameters and warns about overlapping clips and notes past a pattern's end. Full reference: `docs/modules/music/song.org`.
- Top level: `version: 1`; `song: {title, artist, bpm, time_signature: 4/4, sample_rate, seed, loop: {start, end}, tempo: [{beat, bpm, ramp}], markers: [{beat, name}]}`; `instruments: {id: {type: synth|drums|sampler|soundfont|foley, source, params}}` (source = synth preset, kit `"808"` (quote it), or a file); `patterns: {id: {length, steps, notes: ["beat key length velocity"], lanes: {piece: "x...x..."}}}`; `tracks: [{id, kind: audio?, instrument, bus, volume (dB), pan, mute, solo, clips: [{pattern|file, start, length, offset, transpose, gain, fade_in, fade_out}], automation: [{target, points: ["beat value"]}]}]`; song-level `automation:` (targets `bus.<bus>.volume`, `effect.<bus>.<id>.<prop>`); `mixer: {buses: [{name, parent, effects: [{type, id, params}]}], ducking: [{trigger, target, threshold, depth, attack, release}]}` (the `.rmixer` vocabulary).
- Beats are quarter notes from 0; keys are note names (`A1`, `C#4`) or MIDI numbers; step characters `.` rest, `g` 0.3, `o` 0.5, `x` 0.8, `X` 1.0 (spaces and `|` ignored). Clip files resolve against the song's folder.

## Foley patches (.rfoley), 0.10.0: LrgFoleyPatch
- `lrg_foley_patch_new_from_file` / `_new_from_data`, `_save_to_file` / `_to_data`; errors `LRG_FOLEY_PATCH_ERROR_*` by path (`layers[0].freq: -5 is out of range (10 .. 20000)`). Reference: `docs/modules/audio/foley.org`.
- `version: 1`; `foley: {name, duration, sample_rate, seed, variation, loop, loop_tail, fade, peak, normalize}`; `layers: [{type: body|grain|wash|bed|tone|noise, <param>: value}]` (kebab-case params such as `freq`, `freq-end`, `cutoff-end`; defaults are not written). `foley.layer_types` / `lrg_foley_layer_type_list_params()` list every parameter with its range.
