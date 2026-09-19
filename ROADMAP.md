# Libregnum Roadmap

Last updated: 2026-09-18

## Recently Shipped

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
  with `FFMPEG=1`. The fallback when FFmpeg is disabled is intentional.

Presence of an implementation does not imply every platform/rendering path has
been validated. Headless tests skip cases requiring a display or hardware.

## Remaining Concrete Gaps

### MCP debugging and engine resources

Profiler/debug operations still have placeholder paths in
`src/mcp/tools/lrg-mcp-debug-tools.c`. Detailed configuration and registry
resources remain placeholders in `src/mcp/resources/lrg-mcp-engine-resources.c`.
Full physical input-state reporting also needs work in
`src/mcp/tools/lrg-mcp-input-tools.c`. These are separate from the implemented
ECS and save operations.

### Built-in type registration

`lrg_registry_register_builtin()` in `src/core/lrg-registry.c` still needs its
built-in type mappings populated. Applications currently register the types
needed by their YAML definitions and MCP spawning.

### Template confirmations

The pause menu still needs confirmation states for returning to the main menu
and quitting (`src/template/states/lrg-template-pause-menu-state.c`). Loading
state asset integration is implemented.

### Lighting and weather

- Viewport light culling remains unfinished in
  `src/lighting/lrg-lighting-manager.c`; its lighting pass also needs further
  rendering integration.
- Rain splash particle spawning remains unfinished in `src/weather/lrg-rain.c`.

## Future Directions

- Dedicated dungeon/map generation built on the tilemap and ECS modules:
  BSP, cellular automata, or Wave Function Collapse.
- Network prediction or rollback, after defining simulation determinism and
  synchronization requirements.
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
