# M6 (navigation/AI) + M7 (VFX, terrain, audio, 2D): engine wave (read with brief.md)

Seven agents in parallel: NAV, AIFMT (M6); PART, PARTGPU, TERR, AUDIO, TWOD (M7). Editor tools for
both milestones come in the NEXT wave (nav bake panel + overlay, agent debug, BT editor with live
state, logic state-machine editor, LLM decisions inspector, particle editor with live preview,
weather inspector, terrain sculpt/paint/foliage brushes, water inspector, mixer panel with meters,
attenuation gizmo, rule-tile editor, tile properties, sprite slicer + frame animation editor):
give every feature a complete engine API, format, tests and docs, plus the hooks those tools need
(described in your report). M0-M5 are committed (`git log --oneline origin/master..HEAD | head -60`).
Read the plan's M6/M7 sections (/var/home/zach/.claude/plans/take-a-look-at-hidden-iverson.md), the
gap-analysis rows for your area (docs/editor/gap-analysis.org), AGENTS.md (Testing > GPU tests,
editor runs and screenshots: build-aux/xrun.sh, 1920x1080) and skills/skill-libregnum/references/
engine.md. 3D runtime: LrgEntity3D / LrgWorld3D / components (docs/modules/world3d/*), level nodes and
the registry (docs/modules/level/*, docs/modules/core/registry.org), LrgWorld3DRenderer
(docs/modules/world3d/renderer.org, docs/guides/rendering-pipeline.org). Every new format: YAML load
+ save (byte-stable round trip, unknown keys kept, strict errors with the key path) + a docs page
with an annotated schema. Stubs for your new files are registered; replace them wholesale.

## NAV (M6)
Own: src/pathfinding/** (lrg-nav-poly-mesh*, lrg-pathfinder, lrg-nav-*), new
src/pathfinding/lrg-nav-bake.{c,h} (`.rnav` bake settings: agent radius/height/max slope/max step,
cell size/height, region merge, area types with costs, off-mesh links; bake from an LrgWorld3D's
static geometry (mesh renderers + static physics shapes, filtered by layers/tags) or explicit
triangles; tiled bakes for large levels optional; result cached with the existing LrgNavPolyMesh
cache), src/pathfinding/lrg-nav-agent.{c,h} (path following with corridor/string pulling, steering
(arrive, separation), RVO2/ORCA local avoidance between agents and against static edges, a crowd
manager stepping all agents deterministically), src/world3d/components/lrg-nav-agent-component.{c,h}
(speed, acceleration, radius, height, avoidance priority, destination API, arrived/path-failed
signals, drives the entity or a kinematic body / character controller), level `navigation:` block
(src/level/lrg-level.{c,h} + lrg-level-serializer.c + lrg-level-instantiate.c: reference to a .rnav
and its baked data; instantiate loads the navmesh onto the world: `lrg_world3d_set/get_navigation`
(you own src/world3d/lrg-world3d.{c,h} for that)), debug draw (polygons, links, agent paths, ORCA
velocity obstacles). Tests: tests/test-nav-bake.c, test-nav-agent.c (bake determinism, slope/step
filtering, path over links, ORCA: two agents crossing never overlap, crowd of 100 reaches goals, perf
with taskset). Docs: docs/modules/pathfinding/{nav-bake,nav-agent}.org, docs/guides/navigation.org.

## AIFMT (M6)
Own: src/ai/** (behaviour tree classes, blackboard, decision brain), new src/ai/lrg-bt-asset.{c,h}
(`.rbt`: tree of composites/decorators/leaves with params, leaf bindings to registered C actions
(`lrg_bt_register_action (name, func)`) or script functions (through src/scripting's existing
call API), blackboard schema with typed keys and defaults, subtrees by reference; build an
LrgBehaviorTree; debug state per node per tick for a live editor view), new src/ai/lrg-logic-machine.{c,h}
(`.rfsm` generic logic state machine: states with enter/update/exit actions (registered C or
script), transitions with conditions on blackboard keys, events, hierarchical sub-machines, timers;
debug state), src/world3d/components/lrg-ai-component.{c,h} (runs a .rbt or .rfsm on an entity with
a blackboard; exposes blackboard set/get, current node/state, signals), and the 2D equivalent as a
component on LrgGameObject if cheap (src/ecs/components/). Tests: tests/test-bt-asset.c,
test-logic-machine.c (format round trip, every composite/decorator, leaf binding, blackboard schema,
FSM transitions/events/timers/hierarchy, determinism). Docs: docs/modules/ai/{bt-asset,logic-machine}.org,
docs/guides/ai.org (BT + FSM + LLM decisions overview).

## PART (M7)
Own: src/particles/** (system, emitter, force, pool, billboard renderer), new
src/particles/lrg-particle-asset.{c,h} (`.rparticles`: system -> emitters -> module stack (spawn
rate/bursts, shape (point/sphere/cone/box/mesh surface), initial velocity/size/colour/rotation/
lifetime ranges, over-life modules (size/colour/alpha/rotation by curve or gradient), forces
(gravity, wind, attractor, turbulence, drag, vortex), collision (planes/world raycast), sub-emitters
on death), seedable deterministic RNG per system; builds an LrgParticleSystem), new
src/particles/lrg-particle-ribbon-renderer.{c,h} (trails + ribbons with UV modes) and
lrg-particle-mesh-renderer.{c,h} (instanced mesh particles), a colour-gradient type if none exists
in the engine (src/core/lrg-color-gradient.{c,h}; reuse LrgKeyframeCurve for curves),
src/world3d/components/lrg-particle-component.{c,h} (plays a .rparticles on an entity, renders through
LrgWorld3DRenderer's transparent pass with layers), (registry rows: report them). Tests: tests/test-particle-asset.c
(format round trip, determinism with a seed, module behaviour vs analytic (gravity drop, curve
evaluation), bursts, sub-emitters, renderers under xrun). Docs: docs/modules/particles/particle-asset.org
(+ renderer pages), docs/guides/vfx.org.

## PARTGPU (M7)
Own: new src/particles/lrg-gpu-particles.{c,h} (GPU particle simulation by transform feedback on
GL 3.3: ping-pong VBOs, emit/update/kill in a vertex shader, sorted-free additive/alpha rendering,
capacity 100k+; graylib has no wrapper: load the GL entry points with rlGetProcAddress the way
lrg-light-set.c does for glGetIntegerv, and add a tiny wrapper to deps/graylib only if needed — then
list it for the lead), a subset of the .rparticles module set runnable on the GPU (validate and
fall back to CPU), src/weather/** (structured weather YAML `.rweather`: rain/snow/fog/lightning/wind
layers with intensities, transitions, presets; load/save; drives LrgWeather + particles) (registry rows: report them). Tests: tests/test-gpu-particles.c (under xrun: counts after N steps, emission rate, determinism
of the GPU path within tolerance vs CPU for the supported subset), tests/test-weather-asset.c.
Docs: docs/modules/particles/gpu-particles.org, docs/modules/weather/weather-asset.org.

## TERR (M7)
Own: src/graphics/lrg-terrain.{c,h}, lrg-terrain-detail.{c,h}, lrg-grass-field.{c,h},
lrg-water-surface.{c,h}, new src/graphics/lrg-terrain-asset.{c,h} (`.rterrain`: chunk grid, height
sidecar (16-bit PNG or raw float file), material/splat ids per cell or splat weights textures, holes,
LOD settings; load/save), new src/graphics/lrg-terrain-brush.{c,h} (engine-side editing ops the editor
calls: raise/lower, flatten (to height or sampled), smooth, noise, paint material weights, holes;
radius/strength/falloff curves; undo-friendly: returns the touched region before/after), new
src/graphics/lrg-foliage.{c,h} (scatter layers: mesh/billboard, density per material, slope/height
rules, random seed, paint/erase density map, instanced rendering with LOD/fade distance), new
src/graphics/lrg-water-body.{c,h} (`.rwater` or a level block: plane/river spline/lake polygon,
shading params for LrgWaterSurface), terrain + water + foliage components and level node kinds
(`terrain`, `water` visual params; ask the lead to append LRG_NODE_VISUAL_* kinds if needed),
physics: a heightfield LrgPhysicsShape for the terrain body. Tests: test-terrain-asset.c (round trip,
brush ops vs expected heights, splat normalisation, foliage determinism and density rules,
heightfield shape matches heights). Docs: docs/modules/graphics/{terrain-asset,terrain-brush,foliage,water-body}.org,
docs/guides/terrain.org.

## AUDIO (M7)
Own: src/audio/** (mixer, bus, effects, manager, sound bank), new src/audio/lrg-mixer-asset.{c,h}
(`.rmixer`: buses with volume/mute/solo/parent, sends with levels, effect chains per bus with params
(the existing effects), snapshots with blend times, ducking (side-chain) rules; load/save; apply to
LrgAudioMixer at runtime; meters API (per-bus peak/RMS) for the editor), spatial emitters:
src/world3d/components/lrg-audio-source-component.{c,h} gains attenuation curves (linear/inverse/
logarithmic/custom LrgKeyframeCurve), min/max distance, cone (inner/outer angle, outer gain),
doppler, occlusion via physics raycast (optional), bus routing; the project's default `.rmixer`
reference in project.ryaml (runtime loader; send the lrg-project.c patch if needed). Tests:
tests/test-mixer-asset.c (round trip, routing math, snapshot blend, ducking, attenuation curves vs
analytic, cone gain). Docs: docs/modules/audio/mixer-asset.org, attenuation section, docs/guides/audio.org.

## TWOD (M7)
Own: src/tilemap/** (tilemap, layer, tileset), src/lighting/*2d* + lrg-lighting-manager.{c,h} +
lrg-shadow-caster.{c,h}, new src/tilemap/lrg-rule-tile.{c,h} (autotiling: rule tiles with neighbour
masks (Godot terrain / Unity RuleTile style: 3x3 bitmask, 47-tile and 16-tile presets, random
variants with weights), applied on paint and on full refresh), per-tile flip/rotation bits and
per-tile collision shapes + custom data in LrgTileset (`.rtileset` format: tile properties, collision
polygons, animation frames), isometric and hexagonal tilemaps (coordinate conversion, picking,
drawing order), 2D shadow geometry in the 2D lighting pass (shadow casters from tile collision and
sprite outlines, soft shadows), src/atlas/** (LrgSpriteSheet already exists there: extend it) + new src/atlas/lrg-sprite-animation.{c,h} (`.rsprite`: slicing a
texture by grid or explicit rects with pivots, named frames, frame animations (fps, loop, events);
the 2D LrgAnimatorComponent/sprite component can play them). Tests: test-rule-tile.c,
test-sprite-animation.c (+ tilemap/lighting existing suites), GPU checks under xrun (2D shadows, iso/hex
draw order). Docs: docs/modules/tilemap/{rule-tile,tileset-asset,iso-hex}.org,
docs/modules/atlas/sprite-sheet.org (+ sprite-animation), docs/modules/lighting/2d-shadows.org, docs/guides/2d.org.

## Everyone
- Tests: your stub suites, headless where possible, GPU under build-aux/xrun.sh, plus every existing
  suite you could affect (name them). Measured performance numbers with taskset in docs.
- Visual checks for anything that renders: 1920x1080 screenshots under build-aux/xrun.sh in
  `/tmp/claude-1000/-var-home-zach-source-projects-libregnum/c474e1e8-b6e5-4b4b-bd42-fe50c0433c3f/scratchpad/v090/shots/m67-<agent>-*.png`; LOOK at them.
- Keep the shared build compiling at every save; stub bodies for declared functions first.
- Shared files: registry rows (src/core/lrg-registry.c + docs/modules/core/registry.org) are added
  by the LEAD: put the exact rows (name, get_type, include) in your report, do not edit those files; level files (lrg-level*, lrg-node-visual, instantiate) are
  NAV's: send NAV-independent level patches to the lead. Skill text in your report.
