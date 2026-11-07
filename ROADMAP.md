# Libregnum Roadmap

Last updated: 2026-11-07

## In Progress: v0.9.0 (closing the gap analysis)

One release that closes every engine and editor row of `docs/editor/gap-analysis.org`, built milestone by milestone on `feat/v0.9.0`.

- **Done, M0 foundations**: runtime level module `src/level/` (levels load with `BUILD_EDITOR=0`), core `lrg-math3d`, composite YAML loader (sub-objects, lists, children, `{ref:}`, strict mode) with writers for locales, dialog trees, sound banks and input maps, `LrgDebugDraw`, render-layer cull masks.
- **Done, M1 3D runtime**: `LrgEntity3D` + `LrgWorld3D`, 3D `LrgComponent` hooks, mesh/light/camera/audio/sprite/tilemap/spawn/trigger/skinned components, `LrgWorld3DRenderer` shared by editor and game, `lrg_level_instantiate_3d()`, 3D play-in-editor, runtime prefab references and variants, `guid:` resolution and runtime import settings.
- **Done, M2 rendering**: `.rmat` materials with instances, `.rshader` user shaders and `.rshadergraph` graphs, GGX metallic-roughness PBR, spot lights and spot shadows, `LrgEnvironment` (HDR/cubemap sky, SH irradiance, prefiltered specular, BRDF LUT), reflection probes, volumetric fog, depth prepass + SSAO, clustered decals, our own QEM simplifier + LOD groups, real post-processing with tonemapping and `.rpost` stacks, one `LrgWorld3DRenderer` frame for games and the editor with five new debug views; editor material, shader graph, environment and post-stack editors, volume gizmos, layer picker; `examples/pbr-showcase`.
- **Done, M3 lighting bake**: `LrgLightmapBaker` (multithreaded CPU path tracer with our own BVH and UV2 unwrapper, async with progress and cancellation), `LrgLightmap3D` + `LrgProbeGrid` applied in the lit shader, light bake modes, the level `lighting:` block; the editor Lighting panel and probe volumes.
- **Done, M4 animation**: glTF skins/clips/morph targets, retargeting with `.rbonemap`, `.ranim` graphs with layers, blend trees, IK and root motion run by `LrgAnimatorComponent3D`, `.rseq` sequences with Bézier keys, 2D skeletal animation (`.rskel2d`); editor animation graph, bone map, sequencer and 2D rig editors, clip preview.
- **Done, M5 physics**: `LrgDynamicsWorld` rewritten after Jolt (GJK/EPA, box/convex/mesh/heightfield shapes, BVH broadphase, persistent manifolds, CCD, sequential impulses with warm starting, islands and sleeping, collision layers), fixed/hinge/slider/distance/cone/6-DOF joints, `.rphysasset` ragdolls, PBD cloth, Voronoi destruction, rigid-body/collider/joint/cloth/destructible components, the node `physics:` block and project physics settings; editor physics panel, collider and joint gizmos, Physics overlay, ragdoll editor and the layer matrix; `examples/physics-sandbox`.
- **Done, M6/M7 engine**: `.rnav` bakes from worlds with ORCA crowds and `LrgNavAgentComponent`; `.rbt` behaviour trees and `.rfsm` logic machines on `LrgAiComponent`; `.rparticles` module stacks with ribbons, mesh particles and transform-feedback GPU particles; `.rweather`; `.rterrain` with brushes, foliage and `.rwater`; `.rmixer` with snapshots, ducking and attenuation curves; rule tiles, `.rtileset`, iso/hex grids, 2D shadows and `.rsprite` animations.
- **Next**: M6/M7 editor tools, M8 scripting/profiling, M9 export/plugins, M10 editor workflows and collaboration.

## Recently Shipped

### v0.8.3: asset sources and the game module editor hook (`src/editor/`, `src/editor/ui/`, `src/gamemodule/`)

A tool release for game modules whose assets are not project files (librewow's WoW client archives).

- **`LrgAssetSource`**: a GObject interface for virtual asset trees (`get_id`, `get_title`, `list`, optional `search`, `export_entry`) with `LrgAssetSourceEntry` (path, name, folder flag, the asset type the export produces, size, description); path normalisation, a fallback search that walks `list`, and an export cache (`lrg_asset_source_export_cached()`: one folder per entry under `$XDG_CACHE_HOME/libregnum/editor/sources/<id>/`, a marker file, reused across runs, `invalidate_cache`).
- **Editor registry and Sources panel**: `lrg_editor_app_add_asset_source()` / `remove` / `get` / `lookup`, cached export (optionally to a folder), import into `<project>/assets/<id>/<folder>/` and place in the scene (the Assets panel's "Place in Scene" factored into a shared helper); `LrgEditorSourcesPanel` (dock id `sources`): source chooser, Up and path, folder filter or whole-source search, list with type and size, preview through the thumbnail cache, Import, Place, Export to.
- **Game module editor hook**: `LRG_DEFINE_GAME_MODULE_EDITOR (func)` exports `lrg_game_module_editor_init` in module builds; `lrg_loaded_game_lookup_symbol()`; `lrg_editor_app_init_module()`, which `lrgeditor --module` calls after loading the preferences (a failing hook exits 1).
- **Automation**: six IPC methods (`source.list`, `source.ls`, `source.search`, `source.export`, `source.import`, `source.place`), the `lrgeditorctl source` verb and six MCP tools. See `docs/editor/asset-sources.org`.

### v0.8.2: texture panels and new 3D modes (`src/term/`)

A runtime release for hosts that composite their own windows in 3D (gsurf's `--lrg=3d`, cmacs lrgterm).

- **Texture panels**: `lrg_3d_surface_sync_texture_panel()` shows a host-owned `GrlTexture` on a live window panel by reference (no read-back, crop or upload) while it keeps layout, focus, pins and picking; `panel_texture_updated()` refreshes mipmaps; `set_capture_enabled (FALSE)` drops the flat-frame read-back for hosts with only texture panels.
- **Slots and brightness** on `LrgScenePanel` (`set_slot`, `compare_slot`, `set_brightness`) so arrangements can order and fade panels without knowing the host.
- **Arrangements**: `carousel` (a ring around the viewer, ordinal 0 in front), `grid` (curved wall, one band per group) and `stack` (a Rolodex for history and revisions).
- **Environments**: `sky` (sky dome following the time of day) and `studio` (neutral grey, ringed floor), through a new optional `draw_sky` hook on `LrgPanelEnvironment`.
- **Host hooks**: a desktop HUD callback (`set_overlay_func`), panel coverage for refresh throttling, and named camera bookmarks with `lrg_pose_to_string()` / `lrg_pose_from_string()` for persistence.

### v0.8.1: lrgeditor on the retained UI toolkit (`src/editor/`, `src/editor/ui/`, `src/ui/`)

A tool release: the engine runtime is unchanged and `src/gui/` stays untouched.

- **Editor rebuilt on `src/ui`**: `LrgEditorWindow` (menu bar, toolbar, `LrgDockArea`, status bar, toasts) and one widget per panel (hierarchy, inspector, viewport, assets, console, history, problems, tiles, timeline, profiler, code, palette, preferences, dialogs); menus and shortcuts are GIO actions with GTK accelerators; 0.8.0 `editor.yaml` files are migrated with a backup; `lrg_editor_app_frame()` / `get_gui()` are deprecated but still work. See `docs/editor/overview.org` and `docs/guides/editor-migration-0-8-1.org`.
- **Toolkit additions**: drag and drop (`LrgDragPayload`, `LrgDragSource`, `LrgDropTarget`, canvas sessions), the reflected `LrgPropertyGrid` over `LrgPropertySource` (hints, multi-edit with mixed values, one undo step per drag), `LrgVectorEdit`, `LrgCurveEdit`, `LrgGradientEdit`, tree view multi-select / reveal / inline rename, view drop positions, menu geometry hooks and loader source paths.
- **Headless view model**: `LrgEditorViewState`, `LrgEditorViewportNav` and `LrgEditorPlaySession`, so `view.*`, `play.*`, `timeline.*` and `tilemap.tool` work `--headless`.
- **Viewport**: single, split and quad layouts with per-viewport cameras, render modes (lit, unlit, wireframe, normals, lighting only) and overlays; engine lighting (`LrgLightSet`, cascaded shadows, sky dome, preview post stack); camera picture-in-picture; live inspection and a profiler panel while playing.
- **Inspector and prefabs**: property-grid inspector with multi-node editing, lock and add-component picker; linked prefab instances with override paths, Apply / Revert, sync on save as one undo group, and Relink for 0.8.0 levels.
- **Assets and code**: `.meta` sidecars with stable GUIDs (0.8.0 GUIDs kept) and importer settings, git status badges and branch (`LrgEditorVcs`), audio waveform, font and script thumbnails, and a built-in code panel (`script-editor: internal|external|emacs`).
- **Data editors**: `LrgEditorDocument` (YAML-path edits with undo, unknown keys kept, validation through the engine loaders) behind editors for input maps, locales, sound banks, themes, HUD layouts, UI layouts, dialog trees and procgen dungeons.
- **Automation**: 130 IPC methods (47 new: `view.layout`, `inspector.*`, `hierarchy.*`, `asset.*`, `asset.meta.*`, `vcs.status`, `prefab.*`, `code.*`, `data.*`, `profiler.sample`, ...), matching `lrgeditorctl` verbs and 32 new MCP tools; the protocol version and every 0.8.0 reply are unchanged.
- **Gap analysis**: `docs/editor/gap-analysis.org` compares the editor and engine with Godot 4, Unity 6 and Unreal 5 area by area; its engine roadmap is summarised under [Next: engine gaps](#next-engine-gaps).

### v0.8.0: UI framework overhaul (`src/paint/`, `src/ui/`, `src/gui/`)

Concepts from the Raven UI library (Apache-2.0; reimplemented, no code
copied) brought to both UI stacks:

- **SDF paint core** (`src/paint/`): `LrgPaintList` records rounded
  rectangles with per-corner radii, borders, linear/radial gradients, soft
  shadows, lines, triangles, images and text glyphs as GPU instances;
  `LrgPaintRenderer` draws them with one instanced signed-distance-field
  shader (analytic anti-aliasing), usually one draw call per frame.
- **Springs** (`LrgSpring`, `LrgMotion`): closed-form, frame-rate independent
  motion with snappy/smooth/bouncy/wobbly presets, global speed and reduced
  motion tied to accessibility settings.
- **Themes**: 27 colour roles, 18 metrics, accent derivation, five presets
  (midnight, daylight, catppuccin-mocha/latte, high-contrast), strict YAML
  load/save, runtime switching, and `LrgStyleRule` part x state x class
  rules with CSS-like specificity.
- **Retained UI** (`src/ui/`): layout hints (margin, alignment, fill weight,
  min/max), dirty-flag layout, state flags with spring-blended looks, appear
  transitions, event bubbling, pointer capture, enter/leave, Tab and
  gamepad spatial navigation, a popup layer (placement with flipping, modal
  veil, ghost close), and new widgets: overlay, scroll box, separator, spin
  box, combo box, dialog, toast queue, virtualized list view.
- **Immediate-mode GUI / editor**: drawn through the SDF renderer, with
  shadows under popups and tooltips and `lrg_gui_style_init_from_theme()`.
- Fixed: canvas clicks passed (x, y, button) as (button, x, y); text input
  produced uppercase-only ASCII.
- **Glass and effects**: backdrop blur and clear/frosted/tinted glass
  materials (dual-Kawase, refraction rim, dispersion), eight effect shaders
  (shimmer, aurora, neon, spotlight, ripple, grain, plasma, mesh-gradient)
  plus user GLSL snippets, gradient Bezier wires with glow and travelling
  pulses, 36 vector icons and SVG icons; all reachable from theme style rules
  and YAML looks.
- **Text**: SDF glyphs from a shared, incrementally packed atlas (outline,
  glow, shadow, crisp at any scale), font fallback chains (symbols,
  monochrome emoji, CJK, Arabic, Hebrew), kerning, optional fribidi bidi
  reordering and Arabic shaping; used by `src/ui` widgets and the editor GUI.
- **Declarative UI**: `LrgUILoader` builds widget trees from YAML with
  components, expressions, includes, signal handlers, looks and hot-reload
  diffing.
- **Motion**: layout glide (`animate-layout`), `LrgIsland` morphing panel,
  touch gestures (tap, long press, pan with flick scrolling, pinch zoom).
- **Widgets**: table, tree view, tile view, splitter, expander, wrap box,
  colour picker, rating, date and time pickers, breadcrumb, and a markdown
  label built on `LrgRichText` (rich-text effect tags pass through).
- **Tools**: `LrgUIInspector` (tree snapshots, paths, pick mode, highlight,
  timings, live property edits) with six `lrg_ui_*` MCP tools;
  `examples/ui-theme-editor` (palette from an accent, WCAG contrast check and
  auto-fix, YAML export); `examples/ui-showcase` covers every feature.
- Fixed: `LrgRichText` `[[` escape, `[typewriter speed=N]` and per-span
  effect indexing.
- **Actions and shortcuts**: GTK-style widget-scoped GIO action groups
  (`lrg_widget_insert_action_group()`, `lrg_widget_activate_action()`), the
  canvas's built-in `app` / `ui` actions (quit, show/hide/toggle, page,
  theme, motion, direction, focus, toast), `lrg_canvas_add_shortcut()` with
  accelerator parsing and labels, YAML `shortcuts:` and action handlers, and
  right-to-left layout (`LrgWidgetDirection`, mirrored containers).
- **Menus**: `LrgMenu` (popup and context menus from a `GMenuModel`, check
  and radio marks from action state, submenus with a safe triangle, gliding
  highlight, type-ahead), `LrgMenuBar`, and `lrg_widget_set_context_menu()`.
- **Layout and embedding**: `LrgAnchorLayout` (HUD anchors and offsets),
  `LrgSwitcher` (pages with crossfade, slide and zoom transitions),
  `LrgSizeBox` (overrides, min/max, aspect ratio), `LrgViewport` (render
  textures and 3D scenes inside the layout, pointer mapping, fit modes) and
  `LrgCustomWidget` (paint/step/event callbacks without subclassing).
- **Tool widgets**: `LrgTextEditor` with incremental `LrgSyntax` highlighting
  (C, GLSL, Lua, Python, Bash, YAML, JSON, Markdown, INI and YAML-defined
  languages), `LrgNodeGraph` (typed ports, gradient wires, minimap, body
  widgets, YAML), `LrgTimelineView` (tracks, clips, keyframe curves,
  markers, playback, `lrg_timeline_view_evaluate()`) and `LrgDockArea`
  (tabbed, splittable, floatable panels with YAML layout persistence).
- **Examples**: `examples/ui-showcase` gains a menu bar wired to actions and
  shortcuts, card context menus, `--rtl`, and Layout, Viewport, Editor,
  Graph, Timeline and Dock pages; the new `examples/ui-studio` is a docked
  tool app loaded from `examples/data/ui/studio.yaml` (C fallback) whose
  timeline keyframes animate a live 3D scene, with a console, global
  shortcuts, theme switching and a dock layout saved as YAML.
- **Files, scripting and window chrome**: `LrgFileBrowser` (async GIO
  listing, places, filters, type-ahead, live refresh) and `LrgFileDialog`
  (GtkFileDialog-style async open/save/select-folder, in-app or through the
  XDG desktop portal), `LrgBlockEditor` (Scratch-style snapping blocks with
  a palette, undo, YAML and a walkable program API for game scripting),
  `LrgPagination`, `LrgHeaderBar` (self-drawn title bar with window buttons
  and drag-to-move), `LrgNotification` (freedesktop notifications over GDBus
  with buttons routed to widget actions, falling back to toasts), and toast
  buttons with dismiss reasons.
- **Canvas looks**: Modern, Blueprint, Blender and Neon presets for the
  node graph and timeline (link, pin, key and header styles, glow, dot and
  cross grids) with per-item overrides.
- Not planned: platform look emulation, native accessibility bridges, a
  Vulkan backend, colour emoji and complex-script shaping beyond Arabic.

### v0.6.0

- Stub gaps closed: scene serializer default file I/O; lightmap image
  load/save/upload; 2D lighting viewport culling, camera-derived viewport, a
  real light pass and compositing; rain splash particles and seedable rain;
  pause-menu confirmation dialogs. Fixed template states crashing on push
  (NULL chain-ups), safe state popping during input and a pause-menu widget
  leak.
- Registry built-ins: `lrg_registry_register_builtin()` registers 65 engine
  types (ECS, physics, triggers, lights, particles, weather, shapes,
  definitions, behavior-tree nodes) under kebab-case names; app registrations
  win. MCP debug tools drive the real profiler, overlay, console and
  inspector; `libregnum://engine/config` and `libregnum://engine/registry`
  return live data; `lrg_input_get_state` reports full keyboard, mouse,
  gamepad and input-map state (`lrg_mcp_server_set_input_map()`).
- Editor tools: tilemap painting (Tiles panel; brush, rect, flood fill,
  eyedropper, layers; one undo step per stroke), animation timeline
  (keyframed transform/colour/param channels with easing, scrub/play preview,
  autoplay in play-in-editor, saved as `timelines:` in `.rlevel`) and asset
  thumbnails (textures, models, levels/prefabs; cached per project,
  invalidated on file changes). Reachable via IPC (`tilemap.*`, `timeline.*`,
  `asset.thumbnail`), `lrgeditorctl tiles|timeline|thumbnail`, MCP and Emacs.
  Docs: `docs/editor/editor-tools.org`.
- Procgen module (`src/procgen/`): seeded, integer-only map generation (BSP
  rooms/corridors/doors, cellular-automaton caves, wave function collapse with
  learned or YAML rules and backtracking, drunkard's walk, fixed-point noise
  heightmaps); connectivity, farthest-pair start/exit, spawn placement and ECS
  spawning; tile rules with cardinal/blob autotiling, `LrgNavGrid`
  conversion, YAML `dungeon.yaml` recipes. Example `game-procgen-dungeon`,
  bench `procgen`, docs in `docs/modules/procgen/`.
- Deterministic simulation and replays (`src/replay/`): `LrgSimulation`
  (seeded random stream, per-tick input, desync detector), `LrgSimStateful`
  on random stream/timers/physics, the versioned `LrgReplay` format, template
  `--record/--replay/--replay-headless/--seed`, golden-file tests and a
  GGPO-style `LrgRollbackSession` over loopback and UDP transports. The build
  now uses `-ffp-contract=off` for cross-architecture float agreement.
- Audio robustness (graylib v0.6.0): game-thread audio calls are queued
  lock-free for graylib's `grl-audio` thread with mirrored getters
  (`sfx-calls` per-call max 0.8-1.4 ms -> under 0.1 ms). The intermittent
  `InitAudioDevice` SIGSEGV (miniaudio 0.11.24 PulseAudio NULL sink info) is
  avoided by opening through ALSA first with Pulse as fallback;
  `GRL_AUDIO_BACKEND` / `LRG_AUDIO_BACKEND` / `LrgAudioSettings:audio-backend`
  select it, and games run silently when no device opens.

### lrgeditorctl, editor MCP and the libregnum agent skill

- `lrgeditorctl`: kubectl-style CLI for running editors (`get`/`describe`/`create`/`set`/`edit`/`apply -f`/`delete`, `run`, `play`, `logs -w`, `events`, `call`, `-o yaml|json|table|wide|name`, sessions by PID/path/project, bash completion).
- MCP (mcp-glib): `lrgeditorctl mcp` bridges to a running editor, `lrgeditor --mcp` serves in-process; 39 tools including image screenshots.
- `skills/skill-libregnum` with references; `make install-skills`. The editor binary is now `lrgeditor`.
- Tests: `make test-editorctl`.

### Editor application (`lrgeditor`, `src/editor/ui/`, `src/gui/`, `editors/emacs/`)

A Unity/Godot/Unreal-style level, scene and world editor built from libregnum itself:

- **Immediate-mode GUI** (`src/gui/`): `LrgGuiContext` widgets, popups/modals, tabs, drag and drop, `LrgGuiDock` docking with a saved layout string, Catppuccin themes, a raylib backend; fully testable headless.
- **Editor runtime additions**: undo groups and node-state snapshots, dirty tracking, clipboard, history jump/limit, viewport math (`LrgEditorView`, picking, box select), a headless translate/rotate/scale gizmo, YAML preferences, the action registry with rebindable shortcuts and fuzzy search, level validation.
- **`LrgEditorApp` / `lrgeditor`**: docked hierarchy, inspector, 3D/2D viewport (lit primitives, mesh/sprite assets, icons, adaptive grid, gizmos), assets, console, history and problems panels; command palette and quick open; preferences and keybinding editor; project settings; play-in-editor with exact restore; running the game via the project's run command; autosave and crash recovery; live reload of files edited elsewhere; `--headless`, `--validate` for CI, `--new-project`.
- **External editor**: `$VISUAL`/`$EDITOR` (wrappers like `emacsclient_tty` kept), 17 presets, Godot-compatible `{file}{line}{col}{project}{dir}` templates, terminal wrapping, Neovim remote.
- **IPC**: JSON-lines Unix socket server covering documents, nodes, components, scripts, selection, history, actions, view, play/run, projects, assets, validation and events.
- **Emacs**: `libregnum.el` (open-file routing, outline, inspect, node editing, actions, play controls, log, problems, `libregnum-rlevel-mode`, evil bindings) with an ERT suite.
- Tests: `test-editor-core`, `test-editor-tools`, `test-editor-server`, `test-gui`, `test-editor-app`, `make test-emacs`.

Possible follow-ups: in-viewport multi-view layouts, a material/shader graph editor, a tilemap painting tool, an animation timeline, asset thumbnails, and per-project editor settings overrides.

### Non-networking audit follow-ups

- Added `LrgAudioMixer`: configurable parent buses and cycle-checked sends,
  gain/mute, low-pass and feedback-delay effects, copied/resampled voices,
  looping/pause/stop, headless PCM rendering, and graylib stream refills.
- Added `LrgNavMesh`: triangle-surface baking, slope filtering, edge adjacency,
  nearest projection, dynamic polygon blocking, and A* waypoint corridors.
- Implemented MaxRects and Guillotine atlas packing with bounded bin search.
  All algorithms now support rotation, preserve bounds/padding, and invalidate
  stale results correctly. Added geometry/property regressions and benchmarks.
- Documentation: `docs/modules/audio/mixer.org`,
  `docs/modules/pathfinding/nav-mesh.org`, `docs/modules/atlas/packing.org`.

### MMO server foundations

- Implemented incremental bounded TCP receive and queued nonblocking send paths,
  numeric bind addresses and ephemeral ports; fixed framing and error ownership.
- Added headless realms with account admission, replay/rate checks, idle expiry,
  fixed ticks and capacity-checked in-process zone transfers.
- Added spatial interest replication with acknowledged per-viewer deltas and
  stable entity revisions, plus authorized group membership.
- Added SQLite transactional record batches with optimistic revisions, atomic
  rollback and durable reopen tests. SQLite is now a required dependency.
- Added an executable headless simulation and a source-based gap audit in
  `docs/modules/mmo/index.org`. Identity/TLS gateways, multi-process handoff,
  game-specific social/economy services and production operations remain work
  for the game/deployment; this is not a complete production MMO stack.

### Gameplay timers and non-networking audit

- Added `LrgTimerManager`: one-shot/repeating timers, scaled/unscaled clocks,
  pause/resume, cancellation, and safe callback mutation. Repeats coalesce
  missed intervals with phase preservation.
- Game templates own and advance timers once per frame, respecting pause,
  time scale and hit stop; shutdown cancels pending timers.
- Added scheduler regression tests and headless template integration checks.
  See `docs/modules/core/timer-manager.org` and `docs/engine-audit.org` for the
  audit and the subsequent audio/navigation/atlas implementations.

### Reliability and development workflow

- Inventory additions preserve per-instance metadata. Stacks merge only when
  their item definition and all metadata agree.
- Default A* uses cost-aware Manhattan or octile estimates, retaining cheapest
  routes with fractional/zero costs and diagonal movement. Smoothed paths retain
  the original route cost.
- Event dispatch preserves registration order and listener lifetime, stops on
  cancellation, and rejects events cancelled before emission. Quest callbacks
  detach safely when tracked instances change.
- The sanitizer test runner now fails on undefined behavior. Regression tests
  cover these failures before and after their fixes.
- Loading states now load and cache assets through the asset manager, report
  failures, preserve task callback lifetimes, and complete once per run.
- YAML assets support validated loading and opt-in file monitoring, including
  atomic file replacement. Invalid edits retain the last good cached object;
  reload/error signals let applications update references explicitly.
- Analog bindings report press/release transitions from one snapshot per input
  frame. Engine and template updates poll before input consumers; injected
  software events survive that poll.
- The engine exposes a named world registry. MCP now implements ECS inspection,
  spawn/destroy, transform and component operations, live ECS resources, and
  save/list/load/delete/quick-save/quick-load operations with actual error results.

Documentation: `docs/modules/core/`, `docs/modules/input/`,
`docs/modules/mcp/`, and `docs/modules/template/states/loading.org`.

### Module configuration + `lrgldr` loader (`src/gamemodule/`, `src/launcher/`)

Standardized "configure a loadable module from a CLI-style argument vector":

- **`LrgConfigurable` interface** (`src/gamemodule/lrg-configurable.{h,c}`) —
  `apply_args(argv, error)`. `LrgGameTemplate` implements it and bridges to an
  overridable `apply_args` class vfunc; on success it emits the new
  `LrgGameTemplate::args-applied` signal and records the vector
  (`lrg_game_template_get_applied_args`).
- **`lrg_game_run_standalone` now applies its argv** before startup (previously
  ignored), so standalone games get a CLI for free and the path is unified.
- **`lrgldr`** (`src/launcher/lrgldr.c`, replaces the old `lrg-launcher`): a
  generic loader binary with its own options that forwards everything after `--`
  to the loaded module. Resolves the module from a positional arg,
  `$LRG_GAME_MODULE`, or a compiled default; module args fall back to
  `$LRG_GAME_ARGS`. Supports `--info` and a headless `--dry-run`.
- Tests: `tests/test-configurable.c`, `tests/test-lrgldr.c` (+ the
  `tests/fixtures/test-args-module.c` fixture). Engine version → 0.2.0.

### Reel — Programmatic Video Creation (`src/reel/`)

A declarative, code-driven video framework: define an `LrgReel` composition of
timed, animated `LrgReelClip` layers, then render it frame-by-frame headlessly.
Shipped in v1:

- **Composition & timing**: `LrgReel`, `LrgReelContext` (relative-frame stack),
  `LrgReelSequence` with shift / series / loop / freeze modes.
- **Animation math**: `lrg_reel_interpolate` (multi-segment, all extrapolation
  modes), `lrg_reel_spring` (damped oscillator), reusing the engine easing.
- **Renderer**: `LrgReelRenderer` — CPU/headless compositing with per-clip
  opacity and z-order; streams to exporters.
- **Exporters**: GIF (`LrgReelGifExporter`), PNG/JPEG image sequence
  (`LrgReelSeqExporter`), and MP4/WebM via an ffmpeg subprocess
  (`LrgReelVideoExporter`, runtime-discovered, graceful when absent).
- **Transitions**: CPU `LrgReelTransition` — fade, wipe, dissolve, slide.
- **Audio**: `LrgReelAudioTrack` offline mixing + muxing into MP4.
- **Preview**: `LrgReelPlayer` interactive play/scrub window (the only GPU part).
- Full test coverage in `tests/test-reel.c`; examples in `examples/reel-hello.c`
  and `examples/reel-showcase.c`; docs under `docs/modules/reel/`.

The v2 expansion below adds the CLI renderer, motion blur, and YAML authoring.
A named-reel registry and nested-clip opacity compositing remain future ideas.

### Reel v2 Feature Expansion (`src/reel/`)

Shipped: content clips (solid, gradient, image, text, shape, caption, video),
per-clip transform/blend/effect chain (blur, bloom, color-grade, vignette, grain,
chroma-key, drop-shadow, light-leak), media input (`LrgReelVideoSource`/Clip +
FFT audio analysis), five new transitions (flip, clock-wipe, iris, push, zoom)
and `LrgReelTransitionSeries`, motion blur (`render_parallel`, `render_range`,
`set_motion_blur`), GPU FBO capture (`LrgReelGpuRenderer`), new codecs
(H.265/ProRes/ProRes-alpha/VP9-alpha) and audio-only exporter, OkLab color
interpolation, bezier easing and noise, path animation (`lrg_reel_path_*`), YAML
data-driven authoring (`lrg_reel_load_yaml` + schema), `reel` CLI tool
(info/still/render, codec/crf/threads), and caption/transcription support
(`LrgReelCaptionClip`, `lrg_reel_transcribe_audio`).  Docs in `docs/modules/reel/`.

## Implemented Since the Earlier Roadmap

The previous TODO inventory was stale. These features have working
implementations and should no longer be treated as absent:

- Deckbuilder combat context, card effects/costs/targets, profile and manager
  persistence, status checks, and combat/poker template integration
  (`src/deckbuilder/`, `src/template/`).
- Transition drawing and shaders (`src/transition/`).
- Atlas, sprite sheet, nine-slice, and tutorial YAML persistence
  (`src/atlas/`, `src/tutorial/`).
- Vehicle engine/horn/tire audio (`src/vehicle/lrg-vehicle-audio.c`).
- Graphics and audio settings application (`src/settings/`); application still
  requires the corresponding engine window or audio manager.
- Line, pie, surface, and scatter 3D charts (`src/chart/`).
- FFmpeg-backed video decoding, seeking, and playback (`src/video/`), enabled
  with `FFMPEG=1`. The fallback when FFmpeg is disabled is intentional. The
  audio stream is decoded to 16-bit PCM that the host pulls in sync with the
  picture (`lrg_video_player_read_audio()`); the player opens no audio device.

Presence of an implementation does not imply every platform/rendering path has
been validated. Headless tests skip cases requiring a display or hardware.

## Next: engine gaps

The 0.8.1 gap analysis ([`docs/editor/gap-analysis.org`](docs/editor/gap-analysis.org), section "Recommended roadmap") found that most remaining gaps are engine work rather than editor work. In order of dependency, then of how much each unlocks:

1. **3D runtime scene model** (0.9.x, XL): play-in-editor, timelines, live inspection, lights, audio emitters and runtime prefabs all stop at a 2D game object today.
2. **Composite YAML loader and format writers** (0.9.x, M + S): YAML formats for particles, animation state machines and blend trees, behaviour trees, post stacks, lights, terrain, physics bodies and nav bake settings; `save_to_file` writers for locales, dialog trees and sound banks so the data editors save through the engine.
3. **Material assets, user shaders, PBR, cubemap sky and image-based lighting** (0.9.x, L).
4. **Debug draw, physics debug draw and nav agents with avoidance** (0.9.x, S + M).
5. **Animation wiring** (0.9.x, L): state machine and blend trees driving skinned glTF meshes.
6. **Runtime import settings and runtime prefab references** (0.9.x, M): close the places where the editor previews what the game does not do yet.
7. **Script hot reload, memory profiler and GPU timings** (0.9.x, M).
8. **Export and packaging pipeline** (0.10, L).
9. **3D physics** (0.10, XL), after a maintainer decision on the approach.
10. **Lightmap baker and SH probes, LOD, decals, sub-levels and streaming, a script debugger** (0.10).
11. **Visual scripting runtime, GPU particles, retargeting, a sequencer** (1.0 or later).

## Remaining Concrete Gaps

### Follow-ups from v0.6.0

- 2D shadow geometry is not yet drawn in the lighting pass; casters only feed
  `calculate_shadows`.
- `LrgParticleEmitter` uses the global `g_random`, so rain splash counts are
  seedable but droplet velocities are not deterministic.
- Rollback sessions lack spectators, late join, time synchronization and
  disconnect detection; the UDP transport is unauthenticated.
  `LrgStateBuffer` overlaps the io module's byte reader/writer.
- Procgen WFC is the simple tiled model only (no NxN overlapping patterns,
  at most 64 tiles).
- Editor: no thumbnails for tilesets or CAD (audio, fonts and scripts landed
  in 0.8.1); no tile autotiling, flips or collision editing; timelines pose
  editor nodes, not runtime entities, during play-in-editor.
- Audio: setup calls, pushed streams and non-background music still take
  raylib's audio lock on the calling thread. The ALSA-first device open forces
  the backend by briefly pointing `PULSE_SERVER` at a missing socket because
  raylib owns the miniaudio context config; a graylib-owned context would be
  cleaner.
- The MCP config resource reports the default settings object, not a running
  `LrgGameTemplate`'s own settings instance.

## Future Directions

- UI P3 (visual): backdrop blur and frosted-glass material (dual-Kawase on a
  render texture, integrated with `src/postprocess/`), pointer-reactive
  effect shaders (shimmer, aurora, neon, spotlight), SDF/MSDF text through a
  graylib wrapper for crisp scaling, declarative YAML UI with components and
  hot-reload diffing through `LrgRegistry`/`LrgDataLoader`, and a UI
  inspector exposed in the editor and over MCP.
- UI P4 (widgets): table, tree view, menu/menu bar, colour picker,
  date/time pickers, breadcrumb, expander, splitter, markdown view (on
  `LrgRichText`), a morphing island panel, and `@key` i18n strings.
- Named reel registry and nested composition opacity.
- Extend definition reload workflows to application-specific dependency graphs
  and live instance migration. Current hot reload replaces cached definitions;
  existing object references stay valid until the application replaces them.

Steam and VR fallback implementations are intentional. Base-class virtual
methods such as `LrgSettingsGroup::apply` require subclass implementations and
are not missing concrete settings features.

## MMO service expansion (2026-09-18)

Added optional TLS transport and actual cancellable async client connection;
password/token authentication; SQLite ownership leases, fenced commits, retry
receipts, audit digests and backup; durable guilds/private inboxes/blocks;
fungible escrow buyouts and transfers; client snapshot application, pagination,
interpolation and prediction reconciliation; versioned compressed envelopes;
origin admission and matchmaking. A Linux TLS reference host exercises worker
storage, authenticated requests, metrics and graceful drain.

See `docs/modules/mmo/services.org` for tested contracts and deployment. Remaining
work is explicit in the MMO audit: game-specific authority, multi-host storage
and failover, broader social/economy policies, production capacity/operations,
and content distribution. These changes do not establish MMO-scale guarantees.

## MMO shared-world completion pass (2026-09-18)

Added PostgreSQL records/leases/receipts/audit, checkpoint worker takeover,
server-owned RPG movement/combat state and snapshots, friends/guild chat
redaction, addressed trade/gift escrow, auctions and unique items, fair paged
replication, signed content verification and staged HTTPS installation. Added
real PostgreSQL tests, worker-kill/restore drills and a local TLS load sample.
See `docs/modules/mmo/completion.org` for contracts and remaining operational
inputs; no production deployment, database HA election or UDP/QUIC claim.

### MMO runtime integration

- Added mutually authenticated DTLS datagrams with bounded payloads and replay windows.
- Added verified mailbox recovery, audited moderation, ranked seasons, whole-party matchmaking and a local operator console.
- Connected authoritative simulation, fenced checkpoints and acknowledged replicas in a playable TLS reference host/client.
- Added opt-in Patroni/etcd primary-election, replica-rejoin and quorum-loss tests.
- Documented runtime contracts and live deployment inputs in `docs/modules/mmo/runtime.org`.
