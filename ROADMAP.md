# Libregnum Roadmap

Last updated: 2026-02-09

## Immediate Priorities (Finish What's Started)

These are partially-implemented features with explicit TODOs in the codebase.

### 1. Deckbuilder Combat System Completion

**TODOs: 13** | Priority: High

The biggest gap. `LrgCombatContext` isn't implemented yet, which blocks:

- Effect stack resolution (`src/deckbuilder/lrg-card-def.c:84`)
- Energy/target checking for card plays (`lrg-card-def.c:158, 163`)
- Variable substitution in card text e.g. `{damage}` -> `"6"` (`lrg-card-def.c:218`)
- Save/load for `LrgPlayerProfile` (`lrg-player-profile.c:124, 138`)
- `LrgDeckbuilderManager` save/load to file (`lrg-deckbuilder-manager.c:772, 796`)
- Wild card enhancement in scoring hands (`lrg-scoring-hand.c:289`)
- Joker suit requirements (`lrg-joker-def.c:85`)
- Debuff status checks for player and enemy (`lrg-player-combatant.c:321`, `lrg-enemy-instance.c:327`)
- Enemy AI integration in combat template (`lrg-deckbuilder-combat-template.c:1074`)
- Joker effect application in poker template (`lrg-deckbuilder-poker-template.c:109`)

### 2. Transition Rendering Integration

**TODOs: 8** | Priority: High

All transition effects are implemented logically but not hooked up to graylib rendering:

- Fade transition (`src/transition/lrg-fade-transition.c:200`)
- Wipe transition (`src/transition/lrg-wipe-transition.c:186`)
- Slide transition (`src/transition/lrg-slide-transition.c:273`)
- Zoom transition (`src/transition/lrg-zoom-transition.c:202`)
- Dissolve transition — needs shader rendering (`src/transition/lrg-dissolve-transition.c:191`)
- Shader transition — compile, unload, and render (`src/transition/lrg-shader-transition.c:160, 177, 247`)

### 3. YAML Serialization for Atlas & Tutorial

**TODOs: 8** | Priority: Medium

These modules need yaml-glib `load_from_yaml` / `save_to_yaml` implementations:

- `LrgTextureAtlas` load/save (`src/atlas/lrg-texture-atlas.c:259, 666`)
- `LrgSpriteSheet` load/save (`src/atlas/lrg-sprite-sheet.c:350, 1146`)
- `LrgNineSlice` load/save (`src/atlas/lrg-nine-slice.c:292, 976`)
- `LrgTutorial` load/save (`src/tutorial/lrg-tutorial.c:400, 1162`)

The pattern is well-established in other modules.

### 4. Vehicle Audio Integration

**TODOs: 7** | Priority: Medium

Vehicle audio system is stubbed out, waiting for `LrgAudioManager` hookup:

- Start engine loop (`src/vehicle/lrg-vehicle-audio.c:386`)
- Stop all sounds (`src/vehicle/lrg-vehicle-audio.c:408`)
- General audio integration (`src/vehicle/lrg-vehicle-audio.c:431, 461`)
- Horn sound stop (`src/vehicle/lrg-vehicle-audio.c:450`)
- Engine pitch updates (`src/vehicle/lrg-vehicle-audio.c:509`)
- Tire screech sound updates (`src/vehicle/lrg-vehicle-audio.c:554`)

### 5. Settings Application

**TODOs: 2** | Priority: Medium

Settings are stored but not applied to the engine:

- Graphics settings apply (`src/settings/lrg-graphics-settings.c:169`)
- Audio settings apply (`src/settings/lrg-audio-settings.c:85`)

---

## New Features

### 6. 3D Chart Types

**Priority: Low**

Test TODOs explicitly call out missing chart implementations:

- `LineChart3D`
- `PieChart3D`
- `SurfaceChart3D`
- `ScatterChart3D`

The 2D chart system exists and is tested (`tests/test-chart.c`).

### 7. Atlas Packing Algorithms

**Priority: Low**

Only Shelf packing is implemented. MaxRects and Guillotine algorithms fall back to Shelf with a warning:

- MaxRects algorithm (`src/atlas/lrg-atlas-packer.c:812`)
- Guillotine algorithm (`src/atlas/lrg-atlas-packer.c:818`)

MaxRects especially would significantly improve atlas space efficiency.

### 8. Video Player (FFmpeg)

**Priority: Low**

`LrgVideoPlayer` exists but FFmpeg initialization is stubbed:

- Actual FFmpeg init (`src/video/lrg-video-player.c:397`)

Would enable cutscene support.

### 9. MCP ECS Tools

**Priority: Medium**

MCP integration tools are waiting on `lrg_engine_get_worlds()` API:

- ECS tools (`src/mcp/tools/lrg-mcp-ecs-tools.c:66`)
- ECS resources (`src/mcp/resources/lrg-mcp-ecs-resources.c:66`)
- Full input state reporting (`src/mcp/tools/lrg-mcp-input-tools.c:604`)

Once the engine API exists, this enables live ECS introspection through MCP — a killer debugging feature.

### 10. Input System State Tracking

**Priority: Low**

Input binding needs proper state tracking for press/release detection:

- Press detection (`src/input/lrg-input-binding.c:592`)
- Release detection (`src/input/lrg-input-binding.c:704`)

### 11. Template Improvements

**Priority: Low**

Game template states have some incomplete features:

- Pause menu confirmation dialogs (`src/template/states/lrg-template-pause-menu-state.c:417, 434`)
- Loading state asset manager integration (`src/template/states/lrg-template-loading-state.c:795`)

### 12. Lighting Optimization

**Priority: Low**

- Viewport culling for lights (`src/lighting/lrg-lighting-manager.c:758`)

### 13. Weather Particles

**Priority: Low**

- Rain splash particle spawning (`src/weather/lrg-rain.c:181`)

### 14. Registry Built-in Types

**Priority: Low**

- Register built-in types as they are implemented (`src/core/lrg-registry.c:408`)

---

## Stretch Ideas

### 15. Procedural Generation Module

**Priority: Stretch**

The engine has weather, particles, procedural audio synthesis, and terrain — but no dedicated procgen module for dungeon/map generation. Given the ECS and tilemap systems, a `lrg-procgen` module with BSP, Wave Function Collapse, or cellular automata would fit naturally.

### 16. Multiplayer / Netcode Improvements

**Priority: Stretch**

The networking module exists. Depending on its current state, rollback netcode or a proper client-server prediction model would make the engine viable for real-time multiplayer.

### 17. Hot-Reload for YAML Data

**Priority: Stretch**

Given everything is data-driven via YAML, a file-watcher that hot-reloads definitions (items, quests, dialog, cards) during development would massively speed up iteration.

---

## Documentation Gaps

- `docs/modules/ui/ui-event.md:356` — Canvas Documentation link is a TODO placeholder
- `LrgSettingsGroup` base class has unimplemented `apply()`, `reset()`, `get_group_name()` virtual methods (`src/settings/lrg-settings-group.c:51, 58, 65`)

---

## Notes

- Test coverage is excellent: 75 test files, ~61,714 lines of test code
- VR and Steam stubs are intentional fallback implementations, not bugs
- All items sourced from TODO comments in the codebase as of 2026-02-09
