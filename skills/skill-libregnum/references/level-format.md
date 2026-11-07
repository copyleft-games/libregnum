# Levels, prefabs and projects on disk

Full references: `docs/editor/yaml-format.org`, `docs/editor/level-model.org`, `docs/editor/projects-and-assets.org`.

## Project layout

```text
mygame/
├── project.ryaml        # manifest
├── levels/main.rlevel   # levels (YAML)
├── prefabs/*.rprefab    # reusable node subtrees (same format, type: prefab)
├── scripts/             # lua, py, js (gjs), c (crispy), el
├── assets/              # textures, models (glTF/OBJ), audio, fonts
└── .lrg-editor/         # editor state: autosave/ (not part of the game)
```

`lrgeditor --new-project DIR --name NAME` creates this with a starter level (Sun light, Camera, Ground plane). `project.ryaml` keys (under a top-level `project:` mapping): `name`, `default_level`, `game_output` (the game module `.so`), `run_command` (template with `{project}` `{level}` `{module}`; default `lrgldr {module} -- --level {level}`), `scripts_dir`, `asset_dirs`.

```yaml
project:
  name: Keep
  default_level: levels/main.rlevel
  game_output: build/libkeep.so
  asset_dirs: [assets, levels, prefabs, scripts]
```

## .rlevel

```yaml
type: level
name: arena
default_view: 3d          # or 2d
nodes:
  - name: Floor
    guid: a1b2c3d4-e5f6-7890-abcd-ef1234567890   # stable id, generated
    visible: true
    locked: false
    is_2d: false
    transform:
      location: [0.0, 0.0, 0.0]
      rotation: [0.0, 0.0, 0.0]      # radians, Euler XYZ
      scale: [20.0, 1.0, 20.0]
    visual:
      kind: primitive                # none primitive mesh-asset sprite tilemap light camera audio-emitter prefab-instance cad-part
      primitive: plane               # cube uv-sphere ico-sphere plane cylinder cone torus circle grid rectangle-2d circle-2d
      material:
        color: [0.35, 0.45, 0.35, 1.0]
        roughness: 0.5
        metallic: 0.0
    components:
      - type: health                 # registered component type
        props: { max_hp: 100 }
    scripts:
      - language: lua
        script: scripts/floor.lua
        enabled: true
    children: []
```

- `tilemap` nodes: `asset` is the tileset image, `params.tile_size` the world size of a cell, and an optional `visual.tilemap` holds the painted grid: `{width, height, tile_width, tile_height, layers: [{name, visible, rows: ["1 1 2 0", ...]}]}` (ids 1-based into the tileset, 0 empty, row 0 on top, growing right/down from the node origin).
- An optional top-level `timelines:` list holds keyframe animation: `{name, length, loop, autoplay, tracks: [{node: GUID, channel: location.y, keys: [{time, value, easing}]}]}`; autoplay timelines run during play-in-editor.
- Transforms are local to the parent; the implicit root is not serialized.
- Primitives are unit-sized (1 m); `scale` sizes them.
- Hand edits are fine: the editor reloads a level changed on disk when it has no unsaved changes, and offers Reload / Keep mine otherwise. `libregnum.el` gives these files `libregnum-rlevel-mode` in Emacs.
- `lrgeditor --validate FILE.rlevel` checks a level without a window (exit 1 on errors). Problem codes: missing-guid, duplicate-guid, duplicate-name, unnamed, slash-in-name, non-finite-transform, zero-scale, missing-asset-ref, missing-asset, component-no-type, unknown-component, script-no-path, script-no-language, script-disabled, missing-script.
- Blender scenes exported with the scene YAML serializer import with File > Import Scene or `lrgeditorctl call level.import path=FILE`.

## Runtime

`lrg_level_instantiate()` turns a level into an `LrgWorld` of `LrgGameObject`s (components via the engine registry, scripts as `LrgScriptComponent`, each object tagged with its node GUID); `lrg_level_to_scene()` bakes renderable nodes into an `LrgScene`. Games are shared modules run by `lrgldr MODULE.so -- ARGS`; `lrgeditor --module MODULE.so` loads one so its component types can be edited and played in the editor.

## Prefab references, variants and guid: references (0.9.0)

- `store_prefab_instances: reference` (top of a `.rlevel`, or under `project:` in `project.ryaml`; default `expanded`) saves each linked prefab instance as a reference: root state + a `prefab:` block with `storage: reference` + only overridden / moved / added nodes (other children of a kept node as `{name, guid}` placeholders). Loading rebuilds the full subtree from the `.rprefab` (child guids = `lrg_prefab_derive_guid (root_guid, source_guid)`, as the editor does), so prefab edits reach levels without re-saving. Needs the prefab files at runtime; a missing prefab keeps the bare reference.

```yaml
- name: Crate 1
  guid: 13e1...
  transform: {location: [5,0,-2], rotation: [0,0,0], scale: [2,2,2]}
  prefab:
    source: prefabs/crate.rprefab     # or guid:<uuid>
    guid: <asset guid>
    storage: reference
    map: {13e1...: 5d49...}           # only non-derivable entries
    overrides: [transform/scale, d73f...:transform/location, children/-235f...]
  children:
  - name: Lid
    guid: d73f...
    transform: {location: [0,3,0], rotation: [0,0,0], scale: [1,1,1]}
```

- A prefab variant is a `.rprefab` whose root carries a `prefab:` block pointing at its base; its overrides are its differences. `lrg_prefab_load_resolved()` applies the base chain (max 8 deep with nesting; cycles are errors). Editor: inspector prefab bar "Create Variant" (from an instance), `lrg_editor_create_prefab_variant()` (from an asset); the bar shows `base > variant`.
- Asset references may be `guid:<uuid>` (the guid from the asset's `.meta`). `LrgAssetManager` resolves them through a GUID index of the project's `asset_dirs` (`lrg_asset_manager_set_project_root()`), matching the editor's asset database. Loaders apply `.meta` import settings (texture filter/wrap/mipmaps/max-size, model scale/up-axis/generate-normals, audio mode/volume/loop, font size/codepoints) unless `apply_import_settings: false`.

## Visual params at runtime (0.9.0, used by lrg_level_instantiate_3d)

- light (-> LrgLightComponent): `light_type` (directional/point/spot; default spot when `spot_angle` > 0), `color` [r,g,b] display 0..1, `intensity` (display-space multiplier, not lux), `range`, `spot_angle` (FULL outer cone, degrees), `spot_inner_angle` (FULL inner cone), `cast_shadows` (sun cascades / point cube / spot map within renderer budgets), `shadow_bias` (world units, 0 = renderer default), `layer_mask` (render layers the light reaches)
- camera: `fov`, `near`, `far`, `orthographic`, `ortho_size`, `priority`
- audio emitter: `volume`, `pitch`, `loop`, `autoplay`, `min_distance`, `max_distance` / `radius`
- sprite: `width`, `height`, `facing`, `preserve_aspect` (`pixels_per_unit` ignored)
- tilemap: `tile_size`
- `visual.material` is either the inline block (`color`, `roughness`, `metallic`, since 0.9.0 `emission: [r,g,b]` + `emission_strength`) mapped to the mesh renderer's scalar properties, or a scalar reference to a `.rmat` asset (`material: guid:3f0c…` or `material: materials/crate.rmat`, `LrgNodeVisual:material-ref`), loaded through the engine's asset manager into the component's `material` slot (shared by every node naming it). An unresolvable reference falls back to the scalar material with a debug message.
- primitive/mesh: `size_x..z`, `radius`, `cast_shadows`, `receive_shadows`, `unlit`, `double_sided`

## environment: (0.9.0, optional)
Top-level block read into an LrgEnvironment (lrg_level_get_environment / lrg_level_ensure_environment). Every key optional, unknown keys kept on save, absent block = 0.8 behaviour (file written back without it).
```yaml
environment:
  sky: { source: gradient|color|sky_dome|cubemap|equirect, color: [r,g,b], zenith: [..], horizon: [..], ground: [..],
         path: skies/noon.hdr | guid:<uuid>,   # equirect panorama or cubemap cross
         faces: [px, nx, py, ny, pz, nz], exposure: 0, rotation: 0 }   # exposure in stops; rotation deg about +Y (ignored for sky_dome)
  ibl: { enabled: true, diffuse_intensity: 1, specular_intensity: 1, resolution: 128 }
  ambient: { color: [0.2,0.2,0.2], intensity: 1 }          # flat fallback when IBL is off/unbaked
  fog: { enabled: false, type: uniform|linear|exponential|height, color: [r,g,b,a], density, start, end, height_base, height_falloff }
  volumetric: { enabled: false, density: 0.02, anisotropy: 0.6, steps: 32, max_distance: 100, intensity: 1 }
  post_stack: post/day.rpost | guid:<uuid>
```
Colours are sRGB 0..1 (like all engine colours); .hdr pixels are linear. An unknown sky source / fog type fails the level load (LRG_LEVEL_ERROR_PARSE).
Runtime: lrg_environment_load_sources(env, asset_manager, level_dir, &err); lrg_environment_bake(env, &err) (needs GL); lrg_light_set_set_environment(lights, env); lrg_environment_apply_to_light_set(env, lights); lrg_environment_draw_sky(env, &eye) first inside BeginMode3D; lrg_volumetric_fog_set_from_environment(fog, env).

### Decal and reflection-probe nodes (0.9.0)
- `kind: decal` -> LrgDecalComponent. `asset:` = albedo texture (path/guid); params `size_x`, `size_y` (projection depth, local -Y), `size_z`, `normal`, `orm`, `opacity`, `fade_angle`, `fade_width`, `sort_order`, `normal_strength`, `blend` (alpha|multiply|add); `material.color` tints.
- `kind: reflection-probe` -> LrgReflectionProbeComponent. params `extents_x/y/z` (HALF sizes, default 5, times node scale; axis-aligned), `blend_distance`, `intensity`, `box_projection`, `resolution` (128), `near_clip`, `far_clip`, `auto_recapture`. Needs `lrg_world3d_renderer_capture_probes()` once after the window opens.
- `kind: mesh-asset` whose `<asset>.meta` has `lod-levels > 1` -> LrgLodComponent (engine must be started).
### Environment at runtime
`lrg_level_instantiate_3d()` / `lrg_level_load_world3d()` put a COPY of `environment:` on the world (`lrg_world3d_get_environment`) with sky images loaded, and the `post_stack:` `.rpost` on `lrg_world3d_get_post_stack`. Relative refs resolve: base dir (level file dir) -> asset manager search paths -> project root -> cwd. Missing sky/post = g_warning, not an error. Edit the world's environment at runtime, not the level's.

### Timeline Bézier keys and bone tracks (0.9.0)
- Timeline keys may add `tangent` plus `slope` (user) or `in`/`out` (broken) to make a Bézier key; the segment starting at a Bézier key ignores `easing`.
- Channel `bone.BONE.location|rotation|scale.x|y|z` holds a bind-pose offset (bone names may contain dots).
- Component desc `type: sequence` with params `sequence_path`, `autoplay`, `play_mode`, `rate`.

## lighting: (0.9.0, optional)
- `lightmap: path|guid:`, `probes: [path|guid:]`, `settings: {texels_per_unit, samples, bounces, max_lightmap_size, denoise, probe_spacing}` (numbers; booleans accepted). Instantiate loads them onto the world; missing files are a warning.
- Node params: `static_lighting` and `lightmap_scale` on primitive/mesh-asset nodes; `bake_mode: realtime|mixed|baked` on lights.
