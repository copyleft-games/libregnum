# libregnum 0.9.0: close the gap analysis (engine and editor)

## Context

v0.8.1 shipped `lrgeditor` on the retained UI toolkit, together with `docs/editor/gap-analysis.org`. That doc compares the editor and engine against Godot 4, Unity 6 and Unreal 5 and leaves **112 open items** (32 S, 54 M, 18 L, 7 XL). Zach's /goal: add everything from the roadmap, both engine gaps and editor gaps, as **one big 0.9.0** on one long branch.

Decisions so far:
- **3D runtime:** a new `LrgEntity3D` + `LrgWorld3D`. The 2D `LrgWorld`/`LrgGameObject` stay as they are.
- **Renderer:** stays forward-clustered, gaining PBR/IBL. The engine targets GL 3.3, which has no compute shaders.
- **Runtime level module:** `.rlevel` loading moves out of the editor-only build.
- **Physics:** we write our own by extending `LrgDynamicsWorld`. Jolt and JoltC are cloned into `~/source/public/` as a reference and ported into GObject C (MIT: keep the notices).
- **XL items included:** visual scripting runtime, multi-user live editing, ragdolls/cloth/destruction. **Excluded:** new platforms (macOS/Web/mobile).
- **Material format:** a new `LrgMaterial` runtime asset (`.rmat`). `LrgMaterial3D` stays as the import/authoring struct and converts into `LrgMaterial`.

Key facts from exploration:
- `lrg_level_instantiate()` produces only flat 2D `LrgGameObject`s with X/Y positions. Nothing in the runtime loads `.rlevel` files.
- During play, the editor still draws the authoring nodes, not the runtime world.
- The editor viewport (`src/editor/ui/lrg-editor-scene-renderer.c`) is the engine's most complete 3D renderer (`LrgLightSet`, `LrgShadowRenderer`, `LrgSkyDome`).
- `LrgPostProcessor`'s capture and effect methods are stubs.
- No glTF→`LrgSkeleton` loader exists.
- `LrgDynamicsWorld` supports only sphere, plane and capsule contacts.

## Release structure

- **Branch:** worktree `trees/v0.9.0`, branch `feat/v0.9.0`.
- **Same proven method as 0.8.x:**
  - The lead pre-registers stub files.
  - Agents own disjoint files; builds run under `flock`.
  - The lead commits each feature with registration filtering (`commitfeat.sh` + `pending.txt`).
  - Every feature lands with docs (org), tests (GTest, headless-first, GPU checks under the isolated Xvfb), examples, and a visual pass where it has UI.
- **Gate at the end of every milestone:** clean `make all` (GIR, 0 warnings), `make docs-check` 0 undocumented, full `make test`, `test-editorctl`, `test-emacs`, `test-editor-x11`.
- **Release:** at the end, graylib moves to 0.9.0 in lockstep. graylib gains wrappers for timer queries, transform feedback, cubemaps and MRT where needed.

## Milestones, in dependency order (each one is a wave of ≤8 parallel agents)

### M0: Foundations (lead + 4 agents)
- **Runtime level module:**
  - Move `LrgLevel`, `LrgNode`, `LrgNodeVisual`, the tilemap/timeline data, the serializer, prefab load/instantiate (`lrg_prefab_load_resolved`, `instantiate_linked`) and `lrg-level-instantiate` into an always-built `src/level/`.
  - Keep the old header paths as compat includes, plus `libregnum.h`.
  - The editor keeps editing-only code (commands, selection, gizmo, server…).
  - `BUILD_EDITOR=0` must still build a game that loads levels.
- **Shared math:** move euler compose/decompose and the matrix helpers from `src/editor/lrg-editor-math.c` into core (`src/core/lrg-math3d.{c,h}`). The editor wraps them.
- **Composite YAML loader** in `src/core/lrg-data-loader.c` / `lrg-registry.c`: child lists, typed sub-objects, GUID/path references, strict validation. Plus writers: `LrgLocale`, `LrgDialogTree`, `LrgSoundBank` `save_to_file`. Input-map save keeps order and unknown keys. `*_from_data` loaders for input map, locale, sound bank and HUD. Then switch the 0.8.1 data editors to engine writers where possible.
- **Debug draw:** `src/debug/lrg-debug-draw.{c,h}` (lines, boxes, spheres, capsules, frustums, text; depth-test toggle). **Render layers:** a 32-bit mask on cameras and drawables, honoured by batches, terrain, billboards, particles and shadows.

### M1: 3D runtime (5 agents)
- **Core types:**
  - `src/world3d/lrg-entity3d.{c,h}` (guid, name, quaternion TRS + editor euler, hierarchy, cached world matrix, ordered components, active/visible/static).
  - `lrg-world3d.{c,h}`: GUID map, octree, phases `update`/`fixed_update`/`late_update`/`render(camera)`, and render lists (opaque, cutout, transparent, shadow casters) that can be replayed once per pass.
  - `LrgComponent` gains additive hooks through reserved slots: `attached_entity`, `fixed_update`, `late_update`, `collect_render`, and `lrg_component_get_entity`. Existing 2D components are unchanged.
- **Shared renderer:** extract the runtime parts of the editor viewport into `src/world3d/lrg-world3d-renderer.{c,h}` (primitive/mesh drawing, model/texture caches, light collection, shadow passes, sky, light apply). The editor viewport then uses it, so editor and game draw identically.
- **Components:** mesh-renderer, light, camera, audio-source/listener (spatial), sprite/billboard, tilemap, spawn point, trigger, skinned-model (LrgModelAnimator first).
- **Instantiate:** `lrg_level_instantiate_3d()` maps nodes 1:1 by GUID with full TRS and every visual kind, and creates runtime tilemaps. `LrgNode` gets an `instantiate_3d` vfunc.
- **Play-in-editor 3D:**
  - The play session picks 2D or 3D from the level.
  - 3D baselines with full TRS sync-back (writes only fields that changed; authored euler kept).
  - Viewport draws the runtime world while playing.
  - Live tree model over the entity hierarchy; lookups retyped to GObject in the hierarchy and inspector.
- **Prefabs and assets at runtime:**
  - Runtime prefab references: instantiate from `.rprefab` with overrides.
  - Prefab variants (variant chain + resolver; "Create Variant" and override display in the editor).
  - Runtime GUID resolution and runtime import settings: `.meta` applied by `LrgAssetManager` at load time.
- **Examples:** `examples/level-3d-runtime.c` loads a `.rlevel` in a game and plays it.

### M2: Rendering (7 agents)
- **Materials and shaders:**
  - Material assets `.rmat` (`src/graphics/lrg-material.{c,h}`): shading model, params, texture slots, render state. Material instances/parameter overrides. Binding to batch, terrain, skinned mesh and runtime mesh renderer. Referenced by GUID from levels.
  - User shader assets (`lrg-shader-asset`: GLSL 330 contract + a light-set include, hot reload).
  - Shader graph → GLSL generator on `LrgNodeGraph`.
- **Lighting:**
  - PBR: GGX/Smith/Schlick metallic-roughness, normal and AO maps on all lit paths.
  - 3D spot lights + their shadow maps.
  - Light and environment blocks in `.rlevel`: kinds, shadows, sky, fog, ambient.
  - Cubemap/equirect HDR sky, irradiance + prefiltered maps, BRDF LUT, reflection probes (`src/lighting/lrg-environment.{c,h}`).
  - Volumetric fog.
- **Passes and effects:**
  - Depth prepass, SSAO from depth, forward decals binned in the light clusters (`lrg-decal`), LOD chains (meshopt simplify, screen-size select, cross-fade).
  - Make `LrgPostProcessor` capture/effects real.
  - `.rpost` post stacks with per-level post settings.
  - Extra debug view modes: overdraw, UV, light complexity.
- **Editor tools:**
  - Material editor (sphere/cube preview, slots, drag material onto mesh), shader graph editor, uniform inspector, code-panel shader support.
  - Environment page, reflection-probe/decal/spot gizmos, per-viewport layer picker, LOD preview, import settings for LOD.

### M3: Lighting bake (2 agents)
- **Bakers:**
  - Lightmap baker: UV2 from glTF, or a chart unwrapper ported from xatlas concepts. CPU path tracer (direct + indirect, multithreaded with GThreadPool), denoise (bilateral), atlas storage, applied in the light-set shader, `LrgLightmap` 3D mode.
  - SH probe grid with the same tracer. The 2D `LrgLightProbe` is renamed/kept apart.
- **Editor:** bake panel with progress/cancel, per-object lightmap scale, probe volume placement and visualisation.

### M4: Animation (5 agents)
- **Runtime:**
  - glTF skin/clip import into `LrgSkeleton`/`LrgAnimationClip`.
  - ASM + blend trees drive `LrgSkinnedMesh` palettes through `LrgAnimatorComponent3D`, with an IK post-pass.
  - `.ranim` format (parameters, states, transitions, blend nodes, layers/masks), root motion, morph target weights.
  - Retargeting (`lrg-retargeter`, bone map + rest pose).
- **Sequencer and timeline:** a sequence asset and player (transform, property, camera cut, audio and event tracks) on runtime entities. Timeline Bézier tangents + bone tracks.
- **2D skeletal animation:** bones + mesh deformation, rigging tools.
- **Editors:** state-machine graph, blend space, bone map, sequencer panel, clip preview in assets, IK gizmos.

### M5: Physics (6 agents; Jolt/JoltC cloned to `~/source/public/` as the porting reference)
- **Core solver:** extend `LrgDynamicsWorld`.
  - Collision: GJK/EPA, box/convex hull/triangle-mesh/heightfield shapes, dynamic BVH broadphase, contact manifolds + persistence, CCD for fast bodies.
  - Solving: sequential-impulse solver with warm starting, islands and sleeping.
  - Joints: fixed, hinge, slider, distance, cone, 6-DOF with limits and motors.
  - Collision layers + a project-wide layer matrix.
- **Bodies in the world:** rigid-body and collider components on `LrgEntity3D`. `LrgCharacterController` gets a 3D world integration. The physics body block goes into `.rlevel` and prefabs.
- **Extras:**
  - Debug draw of shapes, contacts and the controller (both worlds).
  - Ragdolls from a skeleton (physics asset).
  - Cloth: position-based dynamics on meshes, attached to bones.
  - Destruction: pre-fractured Voronoi chunks swapped in on impact.
- **Editors:** collider gizmos (2D + 3D), body/shape/joint inspector, layer matrix UI, physics asset editor (ragdoll setup).

### M6: Navigation and AI (3 agents)
- **Navigation:** `.rnav` bake settings + baking from level geometry (agent radius/height/slope/step, areas, off-mesh links). `LrgNavAgent` with path following, steering and RVO/ORCA avoidance.
- **AI formats:** `.rbt` behaviour trees (leaf bindings to C actions/script functions, blackboard schema). A generic logic state-machine runtime + format.
- **Editors:** bake panel + overlay, agent debug, BT editor with live state, state-machine editor, LLM decisions inspector.

### M7: VFX, terrain, audio, 2D (6 agents)
- **Particles and VFX:**
  - `.rparticles` + seedable RNG, using curves (`LrgKeyframeCurve`) and gradients (`LrgGradientStops`).
  - Particle renderers: trails, ribbons and mesh particles.
  - Module-stack VFX authoring.
  - GPU particles via transform feedback (graylib wrapper).
  - Particle editor with live preview.
  - Structured weather YAML + weather inspector.
- **Terrain:** `.rterrain` (chunk grid, height sidecar, material ids), sculpt/flatten/smooth/paint brushes, foliage scatter layers + paint, water body format + inspector.
- **Audio:** `.rmixer` (buses, sends, effects, snapshots) + mixer panel with meters, spatial emitters + attenuation gizmo.
- **2D:** autotiling/rule tiles + rule editor, per-tile flip/rotation/collision, iso/hex tilemaps, 2D shadow geometry in the lighting pass, sprite-sheet slicer + frame animation editor.

### M8: Scripting, debugging, profiling (5 agents)
- **Scripting:**
  - Script hot reload (Lua/Python/GJS/crispy with a state hand-over hook).
  - Script debugger: Lua `lua_sethook`, Python `sys.monitoring`, a DAP server; code-panel breakpoints and Emacs dape.
  - LSP client in the code panel.
  - Visual scripting runtime (`src/scripting/lrg-scripting-graph`: a program format for node-graph and block-editor scripts, an interpreter calling GI-introspected functions and signals) + editors on `LrgNodeGraph`/`LrgBlockEditor`.
  - Editor-time tool scripts (an `editor` GI object in a script console).
- **Profiling:**
  - CPU profiler: hierarchical sections, thread lanes, capture to file, flame view.
  - GPU timer-query lane, memory profiler (GObject counts by type, asset residency, VRAM estimates), network profiler (per-message counters).
  - Frame debugger (draw-call capture list).

### M9: Assets, build, project, extensibility (5 agents)
- **Export:**
  - `lrgpack` (`src/launcher/lrgpack.c`): dependency walk, cooking with texture compression and import settings, rres/VFS packs with content addressing, bundling `lrgldr` + the module.
  - Outputs: Linux tarball, Flatpak, AppImage, Windows zip.
  - Export dialog with profiles and logs.
- **Assets:**
  - Reference scanner + reference viewer, giving safe rename/delete.
  - Reimport on change with cached artefacts.
  - Custom asset types/importers interface.
  - FBX via ufbx (single-file C, MIT, vendored in `deps/`); fix the `.ply` classification.
- **Project:** autoloads/global singletons, per-platform setting overrides (settings layering), project templates gallery, mod project template, Steam settings page.
- **Plugins:** editor plugin discovery/loading (manifest, GModule/GI), GI-friendly panel/menu/action and custom inspector/drawer registration.

### M10: Editor workflows, localization, collaboration (6 agents)
- **Levels:** sub-levels/level instances + async streaming (volumes/cells) + multi-scene editing with an active-level selector. Node tags (`LrgNode` and entities) with filter.
- **Viewport and gizmos:**
  - Pivot modes, vertex/surface snap + drop to floor (ray-vs-mesh), align/distribute, unified transform gizmo.
  - Game view panel with aspect/resolution presets.
- **Inspector:** nested arrays/dictionaries/objects, a second inspector.
- **UI:** visual WYSIWYG UI designer (drag-to-place on the UI-layout editor), UI animation blocks + timeline.
- **Localization:** PO import/export, string extraction (`@key` gather), pseudolocalization.
- **Version control:**
  - Commit/diff/history panel (plus Magit hand-off).
  - git-lfs lock status.
  - `.rlevel` three-way merge driver (`lrgeditorctl merge`, git `merge.<driver>` setup).
- **Multi-user live editing:** session sharing over IPC, an op log by GUID with last-writer-wins plus per-node locks, presence cursors.
- **Automation:** MCP and lrgeditorctl coverage for everything new.

### M11: Release (lead + docs agent)
- **Docs:**
  - Gap analysis: every row updated to "have (0.9.0)" or an honest remainder.
  - Per-module pages: a docs page per new format in `docs/modules`.
  - Narrative docs: tutorial, ROADMAP, CHANGELOG notes, skill references.
- **Examples:**
  - A showcase 3D game example built from an editor project, covering levels, PBR, physics, nav agents and animation.
  - Examples for each subsystem.
- **Release:** graylib 0.9.0 lockstep, full verification, merge, tag and push (Zach approved releases of this kind; I'll still confirm before pushing).

## Documentation and skill (Zach: "good complete docs, and also update our skill")
- **Docs are a milestone gate:** no feature merges without its docs, written in the same commit.
- **Engine features:**
  - Each new engine type or format gets a full `docs/modules/<area>/` page: overview, concepts, a format schema with an annotated example, the full API reference (every public function, property, signal, vfunc, with ownership and nullability), worked C examples, performance notes, pitfalls and cross-links.
  - Every engine feature's page gets a "using it from the editor" section.
- **Editor features:** each editor tool gets a section in `docs/editor/*` plus its IPC methods in `docs/editor/ipc-protocol.org` and its verbs and tools in `lrgeditorctl.org`.
- **Guides in `docs/guides/`:**
  - 3D runtime ("load a level in your game")
  - materials and PBR
  - lighting bake
  - animation pipeline from glTF
  - physics (bodies, joints, ragdolls)
  - navigation
  - VFX
  - terrain
  - export/packaging
  - visual scripting
  - plugins
  - collaboration
  - 0.8 → 0.9 migration (runtime level module, LrgComponent hooks, `LrgMaterial`)
- **Ongoing docs:** `docs/editor/gap-analysis.org` is updated as items close, and the indexes (`docs/index.org`, module indexes) are kept current. `make docs-check` covers all new headers: add the new modules to `DOCS_CHECK_HEADERS`.
- **Skill** (`skills/skill-libregnum/` in the repo):
  - **Updated every milestone, not at the end:** `SKILL.md` (workflow for the 3D runtime, materials, physics, nav, animation, export), `references/engine.md` (new modules, formats, traps), `references/level-format.md` (light/environment/physics/prefab-variant/sub-level blocks, new asset formats), `references/lrgeditorctl.md`, `references/mcp.md` and `references/ipc-protocol.md` (new verbs, tools and methods).
  - **Reference split:** add new reference files where a topic is big (`references/formats.md` for `.rmat`/`.rpost`/`.ranim`/`.rbt`/`.rnav`/`.rparticles`/`.rterrain`/`.rmixer`/sequence/visual-script formats, `references/export.md` for lrgpack).
  - **Install:** run `make install-skills` at each milestone gate so the installed copy in `~/.claude/skills` matches.
  - **Check:** the skill's examples are actually run against a headless editor (lrgeditorctl / MCP) at the M11 gate.

## Conventions and verification (every milestone)
- **Code:** gnu89, tabs, GIR-clean public API (`LRG_AVAILABLE_IN_ALL`), zero warnings.
- **Formats:** every new format gets YAML load + `save_to_file` + round-trip tests + a page in `docs/modules`.
- **Editor UI:** every new editor tool registers its own actions/IPC from its own file and gets canvas-driven tests plus screenshots in a dark and a light theme.
- **Gate:** the milestone gate above, plus `make test-sanitize` on the new suites.
- **Visual sign-off:** after M1, M2, M5 and M10 I send Zach screenshots.

## Critical files
- `src/editor/lrg-level-instantiate.c`, `lrg-editor-play-session.c`, `ui/lrg-editor-scene-renderer.c`
- `src/ecs/lrg-component.{h,c}`, `src/core/lrg-data-loader.c`, `lrg-registry.c`, `lrg-asset-manager.c`
- `src/graphics/lrg-light-set.c`, `lrg-static-batch.c`, `lrg-skinned-mesh.c`, `lrg-sky-dome.c`, `src/lighting/lrg-shadow-renderer.c`, `src/postprocess/lrg-post-processor.c`
- `src/physics/lrg-dynamics.{h,c}`, `src/animation/*`, `src/pathfinding/*`, `src/particles/*`, `src/ai/*`
- New modules: `src/level/`, `src/world3d/lrg-entity3d*`, `lrg-world3d*`, `src/graphics/lrg-material*`, `src/lighting/lrg-environment*`, `lrg-lightmap-baker*`, `src/debug/lrg-debug-draw*`, `src/launcher/lrgpack.c`, `src/scripting/lrg-scripting-graph*`.
