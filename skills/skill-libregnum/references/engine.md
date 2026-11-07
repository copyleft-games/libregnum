# Working on libregnum itself

Authoritative guide: `AGENTS.md` at the root of the checkout (read it before changing code), plus `docs/` (org-mode). This page is the short version and the traps that have actually bitten.

## Layout

| Path | What |
|------|------|
| `src/core/` | `LrgEngine` singleton, `LrgRegistry` (type names -> GTypes; engine types pre-registered at startup), data loader (YAML), asset manager/packs, `lrg-math3d` (shared Euler/quaternion/matrix math, TRS compose/decompose, look-at, projections; plain float-array functions) |
| `src/ecs/` | `LrgWorld`, `LrgGameObject`, `LrgComponent` and built-in components |
| `src/graphics/`, `src/scene/`, `src/shapes/`, `src/lighting/`, `src/postprocess/` | Rendering on graylib/raylib |
| `src/ui/` | Retained widgets (games); `src/gui/` immediate-mode GUI (tools; the editor is drawn with it) |
| `src/level/` | Runtime level module, always built (0.9.0+): `LrgLevel`, `LrgNode` (+ `lrg_node_get_world_matrix`), `LrgNodeVisual`, `LrgNodeTilemap`, `LrgTimeline`, `LrgComponentDesc`, `LrgScriptBinding`, `.rlevel`/`.rprefab` serializer, prefabs (`lrg_prefab_load_resolved`, `instantiate_linked`), `lrg_level_instantiate`. Works with `BUILD_EDITOR=0` |
| `src/editor/` | `BUILD_EDITOR=1` authoring layer: `LrgEditor` (commands, undo, selection), view math/picking (wraps `lrg-math3d`), gizmo, settings, external editor, IPC server, actions, validation, projects, asset database/`.meta`, scene import. The old `src/editor/lrg-level.h` etc. are forwarders to `src/level/` |
| `src/editor/ui/` | `LrgEditorApp`: the editor application (panels, viewport render, dialogs, play mode) |
| `src/editorctl/` | `lrgeditorctl` and the MCP tool table shared with `lrgeditor --mcp` |
| `src/mcp/` | In-game MCP server (`MCP=1`): input injection and `lrg_input_get_state`, ECS, save, debug (profiler, overlay, console, inspector) tools; `libregnum://engine/{info,config,registry}` resources |
| `src/launcher/` | `lrgldr` (game module runner), `lrgeditor` |
| `src/scripting/` | Lua, Python, GJS, crispy (C) backends |
| `src/template/` | Genre game templates (FPS, third person, racing, tycoon...) |
| `src/mmo/`, `src/net/` | Networking and MMO services |
| `editors/emacs/` | `libregnum.el` (+ ERT tests) |
| `deps/` | Submodules: graylib, yaml-glib, mcp-glib, crispy, cad-glib |
| `tests/` | GTest suites (`test-*.c`) and smoke scripts |

## Build and test

```bash
git submodule update --init --recursive
make -j"$(nproc)"                 # library, GIR, lrgldr, lrgeditor, lrgeditorctl
make test                         # all GTest suites
make SANITIZE=1 deps && make SANITIZE=1 lib && make -C tests SANITIZE=1 run-sanitize
make test-editorctl               # lrgeditorctl + MCP against a headless editor
make test-emacs                   # libregnum.el byte-compile (warnings = errors) + ERT
make test-editor-x11              # real window + real X input (Xvfb, xdotool, socat, yq)
make install-skills               # this skill into ~/.claude/skills (SKILLS_LINK=1 to symlink)
```

Options: `DEBUG=1`, `BUILD_GIR=0` (faster), `BUILD_EDITOR=0`, `BUILD_EDITOR_UI=0`, `EDITOR_MCP=0`, `MCP=1`, `CRISPY=0`, `STEAM=1`, `WINDOWS=1`.

## Conventions

- C `-std=gnu89`, gcc, `-Werror` with `-Wnull-dereference -Wformat=2 -Wmissing-prototypes -Wdeclaration-after-statement`: declarations at the top of blocks, prototypes for non-static functions, literal format strings. Fix warnings properly; never silence them.
- Tabs for indentation, GNU-style spacing `func (args)`, `/* */` comments only.
- GObject everywhere: `G_DECLARE_FINAL_TYPE`/`DERIVABLE`, `g_autoptr`, `g_steal_pointer`, properties in kebab-case, `LRG_AVAILABLE_IN_ALL` before public declarations.
- gtk-doc/GIR comment blocks in headers, once per identifier (duplicates make the GIR scanner warn, and the GIR build must be warning-free too).
- graylib boxed types (`GrlColor`, `GrlVector3`...) are freed, not unreffed.
- Docs in org-mode under `docs/`, updated in the same change; Conventional Commits; feature work in a git worktree under `./trees/`.

## Traps

- **Render layers vs LrgRenderLayer.** `LRG_RENDER_LAYERS_*` / `lrg_render_layers_*` (graphics/lrg-camera.h) are the 32 visibility layers filtered by a camera's `cull-mask`; `LrgRenderLayer` (`LRG_RENDER_LAYER_UI`, lrg-enums.h) is LrgRenderer's draw order. Drawables (static batch chunks, billboard batches, terrain chunks, particle systems) test the *active mask*, which only `lrg_camera_begin()` / `lrg_shadow_renderer_begin_pass()` install: render code using raw `BeginMode3D()` must bracket with `lrg_render_layers_set_active_mask()`, and hand-drawn meshes must test `lrg_render_layers_is_visible()` themselves.
- **Debug draw needs a flush every frame.** Record into `lrg_debug_draw_get_default()` (or a world/viewport instance), call `lrg_debug_draw_tick (dd, delta)` in update and `lrg_debug_draw_flush (dd, camera)` (or `_flush_matrices`) in render; one-frame primitives otherwise pile up to a 262144 cap. Call `draw_3d()` at the top level of the 3D mode, never inside `rlPushMatrix()`. Use `set_thickness()`, not `rlSetLineWidth()` (GL 3.3 core only guarantees 1-px lines). Subsystems record on their `LrgDebugCategory` and test `lrg_debug_draw_is_category_enabled()` first.
- **raylib `bool` under gnu89.** `<raylib.h>` without `<stdbool.h>` makes `bool` an int-sized enum while raylib returns a one-byte `_Bool`, so callers read garbage (the editor's mouse buttons read as always held; `RayCollision.hit` read padding). The build force-includes `stdbool.h` and `libregnum.h` includes it first. If you see `typedef enum bool` in effect anywhere, that file is broken.
- **raylib `Matrix` layout.** Fields are named by column-major storage index but declared in row order: convert by field name, never `memcpy` a `float[16]`. `raymath.h` is not gnu89-clean; do not include it.
- **`g_ptr_array_copy (src, NULL, NULL)`** inherits `src`'s free function: freeing the copy unrefs live objects. Pass a ref-copy function.
- **Unix socket paths** over 107 bytes are rejected; keep test sockets in `$XDG_RUNTIME_DIR`.
- **Input bugs** do not show in headless GUI tests (they inject input below raylib/GLFW): use `make test-editor-x11`. Under Xvfb by hand, point `XDG_RUNTIME_DIR` at an empty short directory or GLFW opens the window on your Wayland session.
- **GPU tests, editor runs and screenshots: always `build-aux/xrun.sh`** (isolated Xvfb 1920x1200, `-noreset`, X11 forced, short `XDG_RUNTIME_DIR`, throwaway `XDG_CONFIG_HOME`/`DATA`/`CACHE` so runs never touch `~/.config/libregnum/editor.yaml`; `-k` keeps real dirs). Screenshots at **1920x1080**: `lrgeditor --width 1920 --height 1080`, `lrgeditorctl screenshot out.png --window`; theme via `$XDG_CONFIG_HOME/libregnum/editor.yaml` (`theme: catppuccin-latte`). Recipe: AGENTS.md > Testing > GPU tests, editor runs and screenshots. Without `-noreset` a second window in one process fails with "X11: Failed to open display".
- **Level types are not editor code any more.** Put runtime level/prefab/serializer changes in `src/level/` (never in the `src/editor/` forwarders, which are excluded from the GIR scan); `src/level/` must not include editor headers. `tests/check-level-noeditor.sh` builds `BUILD_EDITOR=0` into `build/noeditor` and proves the level API is exported and runnable without the editor.
- **MCP over stdio owns stdout.** `lrgeditor --mcp` duplicates fd 1 for the protocol and points fd 1 at stderr; never `printf` to stdout in code paths that run under it.

## Registry built-ins

`lrg_registry_register_builtin()` (run by engine startup) maps the engine's concrete, headlessly constructible types to kebab-case names: the C prefix without `lrg_` (`rigid-body`, `point-light2d`, `particle-emitter`, `item-def`), components without `-component` (`transform`, `sprite`, `collider`, `animator`). YAML `type:` fields and `lrg_ecs_spawn_object` resolve them without app registration; app registrations of the same name win. To add a type, append a row to `builtin_catalog` in `src/core/lrg-registry.c` and to the table in `docs/modules/core/registry.org`; `tests/test-registry.c` builds every entry from YAML.

## Extending the editor

- New user-facing command: add an action in `src/editor/ui/lrg-editor-app-actions.c` (id, label, category, default shortcut, callbacks). It appears in menus via the palette, gets a rebindable shortcut, and is callable as `lrgeditorctl run ID` and the `run_action` MCP tool.
- New IPC method: `lrg_editor_server_register_method()` (document methods in `lrg-editor-server.c`, app methods in `lrg-editor-app-actions.c`); document it in `docs/editor/ipc-protocol.org`; if agents should use it directly, add a row to the tool table in `src/editorctl/lrgeditorctl-mcp.c`.
- Any edit to the document goes through `LrgEditor` (commands, or `lrg_editor_snapshot_node()` + `lrg_editor_commit_node_state()`), never by poking nodes directly, so it is undoable and emits `changed`.

## Debug and visibility

- `src/debug/lrg-debug-draw.*`: immediate-mode debug geometry (lines, boxes, spheres, capsules, cylinders, cones, frustums, grids, axes, labels, 2D shapes) with durations, depth-tested/on-top lists and 32 toggleable categories; headless-inspectable. Docs: docs/modules/debug/debug-draw.org. Render layers: docs/modules/graphics/render-layers.org.

- Composite YAML loader + format writers: see references/formats.md.

## 3D runtime (0.9.0): LrgWorld3D / LrgEntity3D

- LrgWorld3D owns a root LrgEntity3D; add top-level entities with `lrg_world3d_add_entity()`. Entities: guid (set once), name, tags, active/visible/static, layer 0..31, local TRS (quaternion x,y,z,w + authored Euler kept by `set_rotation_euler`), ordered children and components.
- Frame: `lrg_world3d_step (dt)` = fixed_update (fixed-step 1/60, max-substeps 8, excess dropped) -> update -> late_update; then per camera `lrg_world3d_collect_render (world, camera, list)` and hand the LrgRenderList to LrgWorld3DRenderer.
- Components work for both 2D and 3D. 3D hooks (former `LrgComponentClass._reserved[0..7]`): attached_entity, detached_entity, fixed_update, late_update, collect_render, get_bounds, world_entered, world_exited. 2D attached/detached fire only for LrgGameObject owners; update fires for both. `lrg_component_get_entity()` / `get_owner_object()` / `get_world3d()`.
- collect_render: `lrg_render_item_init_for_entity (&item, entity, self)`; set kind/handles/material/flags; `lrg_render_item_set_local_bounds()`; `if (lrg_render_list_accepts (list, &item)) lrg_render_list_add (list, &item)`. Handles are borrowed; user_data is owned by the list.
- Lights: fill LrgLight3DDesc and `lrg_world3d_register_light (world, G_OBJECT (component), &d)` in world_entered/late_update; unregister in world_exited. Cameras: `lrg_world3d_register_camera (world, cam, priority)`; `lrg_world3d_get_active_camera()`.
- Traps: remove_child/set_parent(NULL) drop the parent's ref; transform-changed is coalesced inside a world (emitted at phase flushes); call `lrg_world3d_invalidate_bounds()` when a component's bounds change without a move; never remove the root; LrgCamera3D frustum uses near 0.05 / far 10000. LrgComponentClass has no reserved slots left.
- 3D components (`src/world3d/components/`): add with `lrg_entity3d_add_component()`. Drawing components submit render items in `collect_render` and never draw directly. Lights, cameras and listeners register with the world on `world_entered` and follow the entity in `late_update`. Forward is local -Z. Every setting is a GObject property (colours as `color-r/g/b/a`), so `.rlevel` visual params map by name with `_` -> `-`. Assets resolve lazily through an optional `asset-manager`, or through the renderer when the handle is NULL. Triggers emit `entered`/`exited` from `fixed_update` (entity world positions, filtered by `filter-tag` / `filter-component`). Find spawn points with `lrg_spawn_component_find (world, tag, team)`. Cameras are chosen by `priority`; force one with `lrg_camera_component_make_current()`. Everything works headless. Docs: docs/modules/world3d/components.org.
- `LrgWorld3DRenderer` draws an LrgWorld3D: `lrg_world3d_renderer_render (r, world, NULL /* active camera */, NULL /* screen */, 0, 0)` inside BeginDrawing. Flags (SHADOWS|SKY|CLEAR|DEBUG_DRAW by default, plus POST and BATCH_STATIC) and render modes match the editor viewport. Call `lrg_world3d_renderer_release()` before CloseWindow(). The renderer never ends the debug-draw frame. Tests: set `headless` and read `get_pass_count` / `get_recorded_entity`. The editor viewport and play-in-editor draw through the same renderer, so what the viewport shows is what the game draws. Docs: docs/modules/world3d/renderer.org.
- 3D level runtime: `lrg_level_load_world3d (path, world, &err)` loads and instantiates (register component types first with `lrg_registry_register`). One LrgEntity3D per node with the same GUID (`lrg_world3d_find_by_guid`); the world matrix equals `lrg_node_get_world_matrix`. Visual -> component: primitive/mesh -> LrgMeshRendererComponent (no material = editor grey), light -> LrgLightComponent (`light_type`, `spot_angle` is the full cone, halved into `outer-angle`), camera -> LrgCameraComponent, audio emitter -> LrgAudioSourceComponent (`radius` -> `max-distance`), sprite -> LrgBillboardComponent (fixed facing, aspect kept), tilemap -> LrgTilemapComponent; other params map by name (`_` -> `-`). Spawn prefabs with `lrg_level_instantiate_node_3d (lrg_prefab_instantiate (p), parent, world, NULL, &err)`. Draw with `lrg_world3d_step` + `lrg_world3d_renderer_render (r, world, NULL, NULL, 0, 0)`. Custom nodes can override `LrgNodeClass.instantiate_3d`. Guide: docs/guides/load-a-level-in-your-game.org; example: examples/level-3d-runtime.c.

### Lit shader (0.9.0 PBR) traps
- One lit shader (LrgLightSet) for batches, terrain, skinned meshes, mesh renderers, editor viewport: GGX/Smith/Schlick metallic-roughness. Colours in the C API are display/sRGB (never pre-linearize); the shader linearizes with pow 2.2 and re-encodes. Intensity scales the display colour (2 ~ 4.6x linear energy), not lux.
- Uniforms persist per program: a material sets pbrParams (metallic, roughness, occlusion strength, normal scale) / mapFlags / emissionOwn; a draw without a material must restore (0, 0.5, 1, 1) / 0 / w=0 (LRG_LIGHT_SET_DEFAULT_*).
- mapFlags: 1 normal, 2 legacy metallic-gloss, 4 height, 8 ORM (unit 4, glTF R=AO G=rough B=metal), 16 emission map, 32 unlit. emissionMap (unit 5) is ALWAYS sampled: bind white for even glow, black for none. Never write emissionColor from a material (it is the light set's global tint); use emissionOwn=(rgb*strength,1).
- Texture units: 0 albedo, 1 metal-gloss, 2 normal, 4 ORM, 5 emission, 6 height, 11 spot shadows, 12 sun, 13 point, 14/15 light grid, 16+ ENV/SSAO/decal hooks (7..10 on a 16-unit driver).
- Nothing drawn + glGetError 0x502 = a samplerCube on the same unit as sampler2D texture0: hook binders must claim a unit even when inactive; user lit programs need lrg_light_set_bind_shader() after lrg_light_set_apply() every frame.
- User lit shaders: "#version 330" + varyings + "out vec4 finalColor;" + lrg_light_set_get_glsl_library() + main calling lrg_light_shade()/lrg_surface()/lrg_light_pbr(); vertex = lrg_light_set_get_vertex_source(). Don't redeclare texture0/colDiffuse.
- Spot lights are point lights with a cone (same id space): add_spot_light / set_spot_light(NULL -> point). A spot's shadow priority (set_point_light_shadow) needs lrg_shadow_renderer_set_spots(res, n>0, dist); spots never take cubes. Per-light shadow bias in world units (0 = renderer's).
- Light layers: set_light_layers(id, mask) + set_draw_layers(mask) per draw; the sun and ambient ignore layers.

### Post-processing (0.9.0)
- `LrgPostProcessor` is real: `begin_capture` binds an RGBA16F target (RGBA8 fallback; `is_hdr_active`), `end_capture` rebinds the previous target, `render (dt)` runs enabled effects (priority order, ping-pong, blending off) and composites over the bound target (alpha, scaled). Renderers owning a scene texture call `lrg_post_processor_process (p, tex, dt)` and draw the returned texture. `set_auto_resize`, `set_quality` (LrgPostQuality -> each effect's `quality-tier`), `release` before CloseWindow. Headless: state only.
- Effect contract: `apply` runs with the target FBO bound, blending disabled; draw the whole target (`lrg_post_effect_draw_fullscreen`), leave the FBO bound and blending off. Helpers `lrg_post_effect_compile_shader`, `lrg_post_effect_bind_framebuffer`. Never touch GL in `initialize` (effects are added before windows open); compile lazily + implement LrgPrewarmable.
- `LrgTonemap` (ACES default / filmic / Reinhard / none, exposure EV, white point, `linear-input`; CPU twin `lrg_tonemap_apply_rgb`). Base intensity (`lrg_post_effect_set_intensity`, a stack's weight) scales every built-in effect.

### Depth prepass, SSAO, decals (0.9.0)
- `LrgDepthPrepass` (depth + view normals from the main camera; `record (camera, func, data)` or `begin`/`end`; never inside another texture mode) feeds `LrgSsao` (`lrg_ssao_render (ssao, prepass)`, read in the lit shader by `lrg_ssao()`; it darkens indirect light only).
- `LrgDecal` boxes project along local -Y (U along +X, V along +Z, image top at -Z); `LrgDecalSet` selects and bins them per frame (`lrg_decal_set_select`) and is bound with `lrg_light_set_set_decals`. Decal edits after `select()` show next select. `LrgDecalComponent` registers with `lrg_decal_component_get_world_set (world)`; the renderer must call `lrg_decal_component_sync_world` + `select` each frame.
- Custom shaders paste `lrg_ssao_get_glsl()` / `lrg_decal_set_get_glsl()` (define `vec3 lrg_to_linear(vec3)` first) and bind with `lrg_ssao_bind` / `lrg_decal_set_bind (..., LRG_LIGHT_SET_HOOK_UNIT)`. Docs: docs/modules/graphics/{depth-prepass,ssao,decal}.org.

### LOD (0.9.0)
- `LrgMeshSimplify` is our own C QEM edge collapse: it never moves vertices (output indexes a subset of the input, attributes stay valid); seams slide as wedge pairs and never open; borders locked by default; stops at `target-ratio` or `target-error`, whichever first. Headless: `lrg_mesh_simplify_indices()`, `_raylib_mesh()`; `_mesh()` uploads a `GrlMesh`.
- `LrgLodGroup`: up to 8 levels with decreasing "smallest screen size still used" thresholds (a positive last threshold culls), hysteresis, group/instance/global bias (`lrg_lod_set_global_bias`), force-level. Generate with `lrg_lod_group_new_from_model[_with_meta]()`; skinned models are refused. Per-view `LrgLodState` runs a dithered cross-fade.
- `LrgLodComponent` (`group`, or `mesh-asset` + `asset-manager`) selects per render list and packs `lodFade` into `item.user_data`. Renderers call `lrg_lod_bind_fade (prog, lrg_lod_component_get_item_fade (item, NULL))`; shaders paste `lrg_lod_get_glsl()` and call `lrg_lod_fade_discard()`, and depth/shadow programs must too.
- Traps: flat-shaded or per-face-UV meshes barely simplify (every vertex locked); use one render list per view.

### LrgWorld3DRenderer frame (0.9.0 M2)
- Frame order: lights -> environment (bake if dirty; ambient/fog/IBL onto the light set) -> decals -> SHADOW -> PREPASS + SSAO (SSAO on, AO view, or volumetric) -> clear (RGBA16F scene image when POST runs) -> SKY (environment sky else dome) -> OPAQUE -> CUTOUT -> VOLUMETRIC -> TRANSPARENT -> overlays/debug -> POST (lrg_post_processor_process, one pass per enabled effect). Guide: docs/guides/rendering-pipeline.org.
- Look precedence: lrg_world3d_renderer_set_environment/set_post_stack (override, callers/editor previews) > lrg_world3d_get_environment/get_post_stack (world; render()/render_view() only) > lrg_world3d_renderer_set_fallback_look (editor feeds the level look here). render_list() has no world: use the override or fallback.
- Bake + probes on a loading screen: lrg_world3d_renderer_bake_environment (r, &err); lrg_world3d_renderer_capture_probes (r, world) (probes live on the environment; LrgReflectionProbeComponent adds itself); capture_probe() captures one from any list. Probes are picked per item (item bounds centre), not per camera.
- Switches: set_ssao_enabled (default off; tune via get_ssao), set_decals_enabled, set_ibl_enabled, set_volumetric_enabled (runs only if the environment enables it). Decals: world renders bind lrg_decal_component_get_world_set(world); render_list binds lrg_world3d_renderer_get_decal_set().
- Items: the item colour is final (no material multiply; 0.8 squared it). LrgMaterial items: apply/apply_state per draw, not static-batched; LrgMaterial3D items: pbrParams + emissionOwn only. lodFade is set per item from lrg_lod_component_get_item_fade. lrg_world3d_renderer_get_material(r, "x.rmat") is the .rmat cache.
- Debug modes (also LrgEditorRenderMode, same order): overdraw, uv, light-complexity, ao (always runs SSAO), reflections; they skip sky and post and clear to black.
- Profiling: lrg_world3d_renderer_set_timing(r, TRUE) then get_pass_time(r, LRG_WORLD3D_RENDER_PASS_*) (ms, GPU-synced per pass).
- Preview post processor = bloom, tonemap (filmic +1 EV), vignette, fxaa, color-grade.
- Editor: lrg_editor_scene_renderer_set_cull_mask(slot), get_lod_group(asset) (forced level previews), recapture_probes(); decal / reflection-probe nodes and material: refs draw while authoring.

### Animation import and retargeting (0.9.0 M4)
- `LrgGltfAnimation` (headless; raylib's compiled cgltf): `new_from_file/_bytes`, `create_skeleton(skin)`, `create_clip(s)`, `create_skinned_mesh(skin)`, `create_mesh(mesh)`. Shortcuts: `lrg_gltf_load_skeleton(path, skin, &err)`, `lrg_gltf_load_clips(path, &err)`.
- Bone i = skin joint i = skinned-mesh bone i. Bone, track and morph group names all come from `lrg_gltf_animation_get_node_name` (unique: "node_<i>" if unnamed, "<name>_<i>" if duplicated).
- Non-joint ancestors of root joints (the Armature) are folded into the root's bind pose and its channel keys: exact for uniform scale, non-uniform approximated with a warning; an animated Armature is not folded.
- Clip channels: `lrg_animation_clip_set_track_channel` (step/linear/cubic; cubic values in-tangent, value, out-tangent like glTF); channels win over keyframes; components without a channel come from `set_track_base_pose`. Morph weights: `add_weights_channel`/`sample_weights`, applied with `lrg_skinned_mesh_set_morph_group_weights`.
- Per frame: `lrg_skeleton_calculate_world_poses` then `lrg_skinned_mesh_apply_skeleton(mesh, skeleton)`. Inverse binds live on the skeleton. `LrgMorphTargets` (in lrg-skinned-mesh.h) blends lazily on the CPU; on the GPU path set_morph_targets makes vertex buffers dynamic.
- Retargeting: `LrgBoneMap` (`.rbonemap`; explicit pairs + name rules: prefix strip, case, separators/camel case, left/right -> `.l`/`.r`) and `LrgRetargeter` (skeleton-space rest alignment; translation NONE/ROOT/ALL; automatic scale from hip heights; `retarget_clip` bakes linear channels; `retarget_skeleton` is live). Skeletons need bone indices 0..n-1.
- Traps: graylib's raylib is built without `ImageDraw`. Cross-check new import paths against `lrg_gltf_geometry_pose` (pattern in test-gltf-animation).

### Sequences (0.9.0 M4)
- `LrgSequence` (`.rseq`) is played by `LrgSequencePlayer` into an `LrgWorld3D`, or by `LrgSequenceComponent` (registry `sequence`, props `sequence-path`, `autoplay`, `play-mode`, `rate`). Tracks bind entities by node GUID; a player binding override, or `"self"` on the component, retargets them.
- Call `lrg_sequence_player_advance(p, dt)` each frame. `scrub()` fires events (reverse order with `reverse=TRUE`) and starts no sounds; `set_time()` fires nothing.
- Traps: rotation channels are Euler radians (no slerp); unkeyed channels are untouched and `stop()` does not restore poses; bone tracks are bind-pose offsets and need `lrg_sequence_player_bind_skeleton()`; an animation clip is used if `animation-sample` is handled, else if registered with `add_clip`/`load_clips`; camera cuts need an `LrgCameraComponent` on the target and `stop()` hands the camera back (NULL); property tracks key numeric, boolean (switches at 0.5) and enum (rounded) properties only.
- Play a level timeline at runtime: `lrg_sequence_new_from_timeline(tl, fps)`.

### Baked lighting runtime (0.9.0 M3)
- `LrgLightmap3D` (`.rlightmap` + `.hdr` pages, entries keyed by entity/node GUID, uv2 scale/offset) and `LrgProbeGrid` (`.rprobes`, L2 SH, LrgEnvironment's convention) live on the world (`lrg_world3d_set_lightmap` / `add_probe_grid`). `LrgWorld3DRenderer` binds per item: lightmap entry, else `lrg_world3d_find_probe_grid(centre)`, else nothing. Values are linear irradiance/pi and replace only the indirect diffuse term.
- Light modes: realtime | mixed (bounce baked) | baked (direct+bounce in lightmaps; skipped on lightmapped draws only). `LrgLight3DDesc` has a `bake_mode` field.
- Traps: a mesh without texcoords2 reads uv2 (0,0) (one texel for the whole object); custom lit shaders must declare `in vec2 fragTexCoord2;` and call `lrg_baked_set_uv2(fragTexCoord2)`; duplicated nodes get new GUIDs and have no lightmap entry until the next bake; the sampler3D hook unit must never share a unit with a sampler2D (the baked binder claims it first); lightmapped items are not static-batched; on a 16-unit driver env/probes/lightmap/SSAO take 7..10 and decals get no unit.

### 2D skeletal animation (0.9.0)
- LrgSkeleton2D (bones/slots/skins/clips, .rskel2d), LrgSkinnedSprite (CPU-skinned 2D meshes, up to 4 weights, atlas region), LrgSkeleton2DClip, LrgSkeleton2DComponent for LrgGameObject (drawn by lrg_world_draw like sprite components).
- Frame: reset_pose -> clip_apply (A at 1, B at w to crossfade; unkeyed channels untouched) -> update_world -> draw (deforms). The component does this; properties skeleton-path, clip, speed, scale, flip-x/y, default-mix, show-bones; signals event(clip, event), finished(clip).
- Palette = world * inverse(bind), bind = setup-pose world without root; setup pose skins to identity. Auto weights: lrg_skeleton2d_compute_auto_weights (inverse distance to bone segments + neighbour smoothing).
- Traps: one skeleton per component; raylib culls back faces, so draw emits each triangle front-facing (keep that if you hand-draw); rotation keys are not wrapped; draw-order keys must name every slot.

### Lighting bake (0.9.0 M3)
- `LrgLightmapBaker` bakes `LrgLightmap3D` + `LrgProbeGrid`s on the CPU: `g_object_set (lrg_lightmap_baker_get_settings (b), "texels-per-unit", 8.0f, "samples", 256, NULL); lrg_lightmap_baker_add_world (b, world); lrg_lightmap_baker_bake_async (b, cancel, cb, data)`; in cb `bake_finish`, then `lrg_world3d_set_lightmap (world, lrg_lightmap_baker_get_lightmap (b))` + `lrg_world3d_add_probe_grid` for each of `get_probe_grids`. Sync: `lrg_lightmap_baker_bake (b, NULL, &err)`. `progress (phase, fraction)` arrives on the caller's main context, monotonic, ends with DONE/1.0; cancel gives `G_IO_ERROR_CANCELLED`.
- Traps: step the world once before `add_world` (lights register on world_entered); only mesh renderers with `static-lighting` take part and entries need an entity GUID (`lightmap-scale` 0 = probe-lit only); REALTIME lights are ignored, MIXED bounce only, BAKED direct+bounce (probes never hold direct light); soft shadows from `light-radius` / `sun-angle`; texels are linear irradiance/pi, albedo/emission given in sRGB; deterministic per `seed` on any thread count; sky sampled by paths only (use a directional light for the sun).
- Meshes without uv2 are unwrapped by `LrgUvUnwrap` (deterministic; resolution 64 / padding 2 / angle 50). raylib `GenMesh*` has no uv2: draw lightmapped primitives with `lrg_uv_unwrap_new_lightmap_primitive (prim, item->params)` (size in the vertices, world matrix only). `lrg_lightmap_baker_get_object_mesh (b, guid)` returns the baked mesh; `evaluate (p, n, rgb)` probes the light at a point; `G_MESSAGES_DEBUG=all` logs phase timings.

### Character animation (0.9.0 M4)
- LrgAnimGraph (.ranim) is both asset and runtime: layers of LrgAnimationStateMachine (states play a clip, a blend tree or a sub-machine; any-state transitions; exit_time crossing semantics, below 1 = once per loop; triggers consumed; interruption blends from a frozen pose), shared typed parameters, root motion (base layer, `xz`/`xyz`, stripped from the pose), and an IK post-pass (LrgIKGoal two_bone/ccd/fabrik/look_at/foot, world targets, raycast callback).
- Run it with LrgAnimatorComponent3D (registry `animator3d`) on an entity with an LrgSkinnedModelComponent; the animator pushes a palette with `set_palette`, which replaces clip playback (glTF models CPU-skinned, LrgSkinnedMesh GPU-skinned, drawn unlit as a CUSTOM item).
- Traps: one graph per character (`lrg_anim_graph_duplicate`); bones numbered 0..n-1; untracked bones take the bind pose; IK chains list the bones to turn plus an `effector`; bone axis +Y; LrgIKChain pole/target are model space, LrgIKGoal targets world space; additive layers measure against the bind pose; morph weights are not driven by the animator yet; the 2D LrgAnimatorComponent is unrelated.

### 3D physics: broadphase and layers (0.9.0 M5)
- `LrgBroadphase` is the dynamic AABB tree under `LrgDynamicsWorld`: proxies have a tight box, a fat box, a layer, a mask and a user id. Pass the step's displacement to `move_proxy`. Mark sleeping bodies SLEEPING and level geometry STATIC (only active proxies look for pairs). Call `rebuild()` after bulk-loading static geometry.
- Results use tight boxes (grow them for speculative contacts). Queries filter only by the `layer_mask` you pass, not the matrix: use `lrg_physics_layers_get_collide_mask (layers, my_layer)` for "what my layer hits". Ray callbacks return the new maximum distance (smaller clips, <= 0 stops). Never create/destroy/move proxies inside a callback.
- `LrgPhysicsLayers`: 32 slots, symmetric matrix; a pair collides when the matrix allows it AND each body's mask has the other's layer bit. Defaults 0-8: default, static, dynamic, kinematic, character, trigger, debris, ragdoll, projectile. Names can't be numbers or "all".
- `project.ryaml` root `physics:` block: `layers: [..]` (position = index, `''` = gap), `no_collide: [[a, b], ..]` (default everything collides). Games: `lrg_physics_layers_load_from_file (layers, "project.ryaml", &err)`; the editor: `lrg_project_get_physics_layers()`.

### 3D collision shapes and narrowphase (0.9.0 M5)
- `LrgPhysicsShape` (physics/lrg-physics-shape.h; LrgShape is the 2D drawing shape): immutable, shared by reference. Kinds: sphere, capsule(r, half_height of the SEGMENT, along Y), cylinder(r, half_h, Y), box(hx,hy,hz), convex hull (quickhull; `new_convex_hull(points, n_floats, radius, &error)`, keeps the input frame, read the COM from get_mass_properties), triangle mesh (`_full` takes per-triangle material/user ids and an active-edge angle; weld vertices or internal edges stay "active" and catch), heightfield (row-major z*n_x+x, grid from the local origin, LRG_PHYSICS_HEIGHTFIELD_HOLE), compound (add children before use), plane (solid where n.p < d). Mesh/heightfield/plane have no mass (static only).
- Transforms are rigid column-major 4x4 (lrg-math3d). NO scale: bake it into the shape. `get_mass_properties(shape, density, &mass, com[3], inertia[9])`: inertia about the COM, local axes; a hull's convex radius is not in its mass.
- Rays: `lrg_physics_shape_cast_ray` (local) / `lrg_collide_ray` (world); fraction in units of dir; starting inside a solid gives t=0.
- Contacts: `lrg_collide_shapes_all(a, xf_a, b, xf_b, spec, GArray<LrgContactManifold>)`: normal A->B, depth > 0 overlap, < 0 speculative, up to 4 points, feature_ids stable across steps. Use `_all` for compounds/meshes/heightfields. `lrg_collide_closest_points` (GJK/EPA, negative = depth), `lrg_collide_overlap`, `lrg_collide_cast(shape, xf_from, xf_to, target, xf_target, &toi, &hit)` (conservative advancement CCD with rotation).
- Debug/gizmos: `lrg_physics_shape_build_debug_mesh(shape, segments)` (local triangle soup, 9 floats per triangle). Docs: docs/modules/physics/physics-shape.org, collide.org.

### 3D physics: LrgDynamicsWorld solver (0.9.0 M5)
- `lrg_dynamics_world_add_body (world, shape, kind, mass, pos, rot|NULL, &id, &err)`; shapes are shared/immutable. Positions are the SHAPE ORIGIN; the solver works on the centre of mass (`get_center_of_mass`). Dynamic needs a volume shape (no mesh/heightfield/plane). Gravity defaults to ZERO.
- Ids: slot | generation<<24 (first ids 0,1,2...; removed ids never return). No body cap. Store your entity in `set_user_data` (guint64).
- Step: `lrg_dynamics_world_step (world, frame_dt, &n, &err)` runs fixed 1/60 substeps (cap 4); render with `get_interpolated_pose`. Per-substep logic: "pre-substep" signal.
- Move: forces/impulses/velocities for dynamic; `move_kinematic (pos, rot, dt)` every frame for kinematic (set_pose teleports and does not push). Fast+small => `set_ccd`.
- Layers: `set_layers (LrgPhysicsLayers)` + `set_body_layer (id, layer, mask)`; single pairs `set_collision_between`. Queries take a layer_mask and an ignore_body: `raycast`/`raycast_all`/`shape_cast`/`overlap_shape`/`overlap_aabb`; hit.body is the id, t in multiples of |dir| (use unit dirs).
- Events (queued, emitted after step, only computed while connected): contact-added (LrgDynamicsContact, A->B normal, relative_speed), contact-removed, sensor-entered/exited (`set_sensor`). Legacy `get_contact` keeps the "pushes A out" normal.
- Sleep is per island (default |v|^2+|w|^2 < 9e-4 for 30 substeps); contacts persist while asleep. Shape bodies default friction 0.5, damping 0.05; 0.8 primitives use the world material (friction 0).
- Joints: subclass LrgPhysicsConstraint (setup / warm_start / solve_velocity / solve_position / post_solve / reset_warm_start / debug_draw) on LrgSolverBody views (world frame = id G_MAXUINT, inv_mass 0, never NULL); `lrg_dynamics_world_add_constraint`. Never add/remove bodies or constraints inside a vfunc; disable instead.
- Tuning: `lrg_dynamics_world_get_contact_solver()` (velocity-iterations 10, position-iterations 2, slop 0.02, speculative 0.02, restitution-threshold 1 m/s). Debug: `lrg_dynamics_world_debug_draw (world, draw, LRG_DYNAMICS_DEBUG_ALL)`; env LRG_DYNAMICS_DEBUG=time|match. Single-threaded; ~80 bodies/ms on a 1000-box pile.

### 3D physics: joints and ragdolls (0.9.0 M5)
- `LrgJoint` (an `LrgPhysicsConstraint`) is the only joint type: kinds fixed / point / hinge / slider / distance / cone / swing-twist / six-dof. Every kind but distance is a 6-DOF preset, so any joint takes every axis call. Build with `lrg_joint_new_<kind> (world, a, b, anchor, axis, …, &err)` AFTER placing the bodies (frames are captured from the current poses), then `lrg_dynamics_world_add_constraint (world, LRG_PHYSICS_CONSTRAINT (j), NULL, &err)`. `b` may be `LRG_DYNAMICS_WORLD_BODY`. Files/editors use `lrg_joint_new (kind, a, b, pos_a, rot_a, pos_b, rot_b)` with local frames (anchors from the body ORIGIN, not the COM).
- The joint frame's X axis is the hinge / slider / twist / cone axis; rotation is split into twist (ROTATION_X) and swing (ROTATION_Y/Z). Measurements are body B relative to body A. `lrg_joint_set_limits (j, axis, min, max)` makes the axis limited (locked when min == max); cone swing limits are symmetric (use `LRG_JOINT_SWING_PYRAMID` for asymmetric ones).
- Motors: `lrg_joint_set_motor (j, axis, VELOCITY|POSITION, max_force)` + `set_motor_target_velocity/position` (rotation targets compose Rz*Ry*Rx; `set_target_orientation` takes a quaternion). Position motors are springs (`set_motor_spring`, default 2 Hz, damping 1) relative to the row's effective mass. Soft limits: `set_limit_spring` (translation axes; on a distance joint TRANSLATION_X, min == max = spring/damper). Friction: `set_friction`.
- Breakable: `set_break_force/torque`; the joint disables itself and emits `::broken (force, torque)` from inside the step: never add/remove bodies or constraints in that handler. `lrg_joint_repair()` re-enables. `lrg_joint_measure (j, world, t[3], angles[3], &err)` reads the current coordinates; `get_applied_force/torque` the last substep's load.
- Chains stretch under fast motion (≈1 % of link length at 2 position iterations): raise `lrg_contact_solver_set_position_iterations()`, not velocity iterations.
- Ragdolls: `LrgPhysicsAsset` (`.rphysasset`) + `LrgRagdoll`. `lrg_physics_asset_new_from_skeleton (skel, total_mass, radius_ratio)` generates a starting asset (stabilised masses); `lrg_ragdoll_new (asset, skel, &err)` → `set_root_transform (model→world)` → `add_to_world` (bodies from the skeleton's CURRENT world poses, layer `LRG_PHYSICS_LAYER_RAGDOLL`, parent/child collisions off) → per frame `lrg_ragdoll_write_pose()` after the world step. Joint limits are measured from the BIND pose. Powered: `set_mode (LRG_RAGDOLL_POWERED)` + `drive_to_pose (ragdoll, animated_skeleton)` each frame (pass the animated skeleton, not the one write_pose writes). Per-bone blend `set_bone_blend` (0 anim, 1 physics) blends with the skeleton's current local pose. Keep child/parent mass ratios near 1 (`lrg_physics_asset_stabilize`); powered motors need high damping (default 10 Hz, damping 8) because the spring sees one part but moves the whole chain.

### 3D physics on entities (0.9.0 M5)
- `lrg_world3d_ensure_physics (world)` attaches an `LrgDynamicsWorld` (gravity -9.81 Y, default layers); the world steps it inside its fixed updates; never step it yourself. `lrg_level_load_world3d()` attaches it for levels with physics blocks.
- Components: `LrgRigidBodyComponent` (registry `rigid-body3d`: kind, mass, friction, restitution, damping, gravity_factor, ccd, layer name, interpolate), `LrgColliderComponent3D` (`collider3d`: sphere / capsule (half_height = half the segment, along Y) / cylinder / box (half extents) / convex / mesh / plane, offset, rotation in degrees, trigger flag), `LrgJointComponent` (`joint3d`: LrgJointKind, target entity or `target_guid`, anchor/axis in entity space, limits, spring, motor, break_force). (`rigid-body`/`collider` are the 2D types.)
- Colliders + a rigid body make one body (a compound if several); colliders alone make a static body; each trigger gets its own sensor body. Bodies are built lazily in the next fixed update and rebuilt when colliders, settings or world scale change (scale is baked into the shape).
- Static/kinematic bodies follow their entity (kinematic pushes others); a dynamic body drives its entity, and writing that entity's position teleports the body. Rendering is interpolated (`LrgWorld3D:physics-interpolation`). Keep dynamic entities top-level (a moving parent reads as a teleport); mesh colliders on dynamic bodies become kinematic.
- Signals after the step: `trigger-entered`/`trigger-exited`, `collision-entered`/`collision-exited` (only tracked while connected), `broken`. `lrg_world3d_find_body_entity (world, hit.body)` maps a query hit to its entity; `lrg_world3d_debug_draw_physics (world, dd, LRG_DYNAMICS_DEBUG_SHAPES|...)` (2D: `lrg_physics_world_debug_draw()`). "No body" is `LRG_PHYSICS_NO_BODY` (= `LRG_DYNAMICS_WORLD_BODY`); body id 0 is valid.
- Character controller: `lrg_character_controller_set_physics (cc, physics, mask)` + `set_ignore_body (own_body)`; `move.ground_body` is the body stood on; it does not push dynamic bodies (give the character a kinematic body).
