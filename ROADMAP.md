# Libregnum Roadmap

Last updated: 2026-10-29

## Recently Shipped

### Unreleased: UI framework overhaul (`src/paint/`, `src/ui/`, `src/gui/`)

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
- Editor: no thumbnails for audio, fonts, scripts, tilesets or CAD; no tile
  autotiling, flips or collision editing; timelines pose editor nodes, not
  runtime entities, during play-in-editor.
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
