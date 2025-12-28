# Libregnum Engine Roadmap

This document outlines the features needed to make libregnum ready for publishing commercial games on Steam.

**Target Games**: RPGs, tycoon games, economy simulators, idle games, FPS, TPS
**Common Elements**: Driving/vehicles, economy systems
**Multiplayer**: Single-player, local co-op, online co-op (no competitive)
**Platforms**: Linux + Windows

---

## Current State

Libregnum has **23 implemented modules**:

| Category | Modules |
|----------|---------|
| Core | Engine, Registry, DataLoader, AssetManager |
| Graphics | Window, Renderer, 7 Camera types, Drawable |
| ECS | GameObject, Component, World, Transform/Sprite/Collider/Animator |
| Input | Keyboard, Mouse, Gamepad, InputMap/Action/Binding |
| UI | 8 widgets, 3 layouts, Theme, Canvas |
| Audio | SoundBank, MusicTrack, AudioManager |
| Tilemap | Tileset, TilemapLayer, Tilemap |
| Dialog | DialogNode, DialogTree, DialogRunner |
| Quest | QuestDef, QuestObjective, QuestInstance, QuestLog |
| Inventory | ItemDef, ItemStack, Inventory, Equipment |
| AI | BehaviorTree, Blackboard, BT nodes |
| Pathfinding | NavGrid, NavCell, Path, Pathfinder (A*) |
| Physics | PhysicsWorld, RigidBody, CollisionInfo |
| Save | Saveable, SaveContext, SaveGame, SaveManager |
| I18N | Locale, Localization |
| Networking | NetMessage, NetPeer, NetServer, NetClient |
| World3D | Level3D, Octree, Sector, Portal, Trigger3D, SpawnPoint3D |
| Scene | Scene, SceneObject, Blender serializer |
| Mod | ModManifest, Mod, ModLoader, ModManager |
| Scripting | Lua, Python, PyGObject, Gjs backends |
| Debug | Profiler, DebugConsole, DebugOverlay, Inspector |
| Shapes | 2D and 3D primitives |

---

## Phase 1: Steam-Ready Minimum

**Priority**: CRITICAL
**Description**: Minimum requirements to release any game on Steam.

### 1.1 Steamworks SDK Integration

**Module**: `src/steam/`

Steam requires integration with their SDK for core features.

| Feature | Description | Classes |
|---------|-------------|---------|
| Steam Authentication | Verify user identity and game ownership | `LrgSteamClient` |
| Steam Cloud | Remote save file sync | `LrgSteamCloud` |
| Steam Achievements | Achievement unlocking and display | `LrgSteamAchievements` |
| Steam Leaderboards | Global/friend score rankings | `LrgSteamLeaderboards` |
| Steam Stats | Persistent player statistics | `LrgSteamStats` |
| Steam Overlay | In-game Steam UI access | (via SDK callbacks) |
| Steam Rich Presence | Show player status to friends | `LrgSteamPresence` |
| Steam Input API | Unified controller config (300+ devices) | `LrgSteamInput` |
| Steam DRM Wrapper | Optional copy protection | (build-time) |

**Dependencies**: Steamworks SDK (C API, proprietary but free)

---

### 1.2 Settings/Options System

**Module**: `src/settings/`

Players expect comprehensive settings menus.

| Category | Settings |
|----------|----------|
| Graphics | Resolution, fullscreen/windowed/borderless, VSync, FPS limit, quality presets (Low/Medium/High/Ultra), texture quality, shadow quality, anti-aliasing, particle effects, view distance, ambient occlusion, motion blur, bloom |
| Audio | Master volume, music volume, SFX volume, voice volume, mute toggle, audio device selection, subtitle language |
| Controls | Full key remapping UI, controller button remapping, mouse sensitivity (X/Y), controller sensitivity, invert Y-axis, controller vibration, dead zone adjustment |
| Gameplay | Difficulty selection, auto-save toggle, screen shake toggle, camera sensitivity, HUD scale |

| Class | Description |
|-------|-------------|
| `LrgSettings` | Settings container with GObject properties |
| `LrgSettingsSerializer` | Save/load to YAML |
| `LrgSettingsUI` | Pre-built settings menu using UI widgets |
| `LrgGraphicsSettings` | Graphics setting group |
| `LrgAudioSettings` | Audio setting group |
| `LrgControlSettings` | Controls setting group |

---

### 1.3 Accessibility Features

**Module**: `src/accessibility/`

**Legal requirement**: European Accessibility Act mandates game accessibility from June 2025. CVAA requires accessible in-game communication.

#### Visual Accessibility

- Colorblind modes (Deuteranopia, Protanopia, Tritanopia)
- High contrast mode
- UI scaling (50%-200%)
- Font size options
- Screen reader support for menus
- Photosensitivity warning + reduced effects mode
- Adjustable screen shake intensity

#### Audio Accessibility

- Full subtitles with speaker identification
- Closed captions (sound effect descriptions)
- Visual audio cues (for hearing impaired)
- Mono audio option
- Subtitle background opacity
- Subtitle text size

#### Motor Accessibility

- Full control remapping (have binding system, need UI)
- One-handed control schemes
- Hold vs toggle options for all actions
- Auto-aim/aim assist options
- Adjustable input timing windows
- Switch/adaptive controller support

#### Cognitive Accessibility

- Objective reminders
- Difficulty modifiers (individual toggles)
- Clear waypoints/navigation aids
- Pause during cutscenes
- Skip button for all cutscenes/dialogue
- Tutorial replay options

| Class | Description |
|-------|-------------|
| `LrgAccessibilitySettings` | Accessibility preferences |
| `LrgColorFilter` | Post-process colorblind filters |
| `LrgSubtitleManager` | Subtitle rendering with styling |
| `LrgScreenReader` | Text-to-speech integration |

**Dependencies**: espeak or speech-dispatcher (for screen reader)

---

### 1.4 Crash Reporting

**Module**: `src/crash/`

Capture and report crashes from production builds.

| Feature | Description |
|---------|-------------|
| Crash dump generation | Capture minidumps on crash |
| Crash reporter UI | User-facing crash dialog |
| Stack trace symbolication | Readable crash traces |
| Crash telemetry upload | Send to server/BugSplat |
| Exception handling | Graceful failure + logging |
| Hardware info collection | GPU/CPU/RAM in crash reports |

| Class | Description |
|-------|-------------|
| `LrgCrashReporter` | Crash capture and reporting |
| `LrgCrashDialog` | User-facing crash dialog |

**Implementation Notes**:
- Signal handlers for SIGSEGV, SIGABRT, etc.
- Integration with GLib's logging system
- Optional: Integrate with BugSplat (free for indie)

---

### 1.5 Game State Management

**Module**: `src/gamestate/`

Scene management exists in graylib, but game-level state is missing.

| Feature | Description |
|---------|-------------|
| Game state machine | Menu, Playing, Paused, Loading, etc. |
| Main menu framework | Title screen, options, quit |
| Pause menu | In-game pause |
| Loading screens | Async loading with progress |
| Splash screens | Logo sequences |
| Credits screen | Scrolling credits |

| Class | Description |
|-------|-------------|
| `LrgGameState` | Abstract game state |
| `LrgGameStateManager` | State stack |
| `LrgMainMenu` | Pre-built main menu |
| `LrgPauseMenu` | Pre-built pause menu |
| `LrgLoadingScreen` | Async loading UI |
| `LrgSplashScreen` | Logo sequences |
| `LrgCreditsScreen` | Scrolling credits |

---

### 1.6 Windows Cross-Compilation

**Build System Changes**

| Feature | Description |
|---------|-------------|
| MinGW-w64 toolchain | Windows cross-compiler on Linux |
| Windows build target | `make windows` target |
| Windows CI/CD | GitHub Actions workflow |
| Windows dependencies | glib, raylib for Windows |
| Windows installer | NSIS or similar |
| Windows testing | Wine or VM testing |

**Files to Create/Modify**:
- `config-mingw.mk` - Windows cross-compilation config
- `Makefile` - Add `windows` target
- `.github/workflows/build-windows.yml` - CI workflow

**Dependencies** (Fedora): `mingw64-glib2`, `mingw64-gcc`, etc.

---

## Phase 2: Genre-Specific Systems

**Priority**: HIGH
**Description**: Systems needed for your target game genres.

### 2.1 Economy/Resource System

**Module**: `src/economy/`

Core for tycoon games, economy simulators, and idle games.

| Feature | Description |
|---------|-------------|
| Resource types | Define currencies, materials, etc. |
| Resource storage | Player/entity inventories |
| Production chains | A + B → C recipes |
| Market simulation | Supply/demand pricing |
| Trade system | Buy/sell transactions |
| Income/expenses | Periodic resource flow |
| Resource UI widgets | Currency display, resource bars |
| Offline accumulation | Idle game offline progress |

| Class | Description |
|-------|-------------|
| `LrgResource` | Resource definition (name, icon, max, etc.) |
| `LrgResourcePool` | Storage container |
| `LrgProductionRecipe` | Crafting/production recipe |
| `LrgProducer` | Entity that produces resources |
| `LrgConsumer` | Entity that consumes resources |
| `LrgMarket` | Price simulation |
| `LrgEconomyManager` | Global economy state |
| `LrgOfflineCalculator` | Calculate progress while away |

---

### 2.2 Building/Placement System

**Module**: `src/building/`

Core for tycoon games, city builders, base builders.

| Feature | Description |
|---------|-------------|
| Grid placement | Snap to grid |
| Free placement | Free-form positioning |
| Placement preview | Ghost building visualization |
| Validity checking | Can place here? |
| Building rotation | Orientation control |
| Building upgrading | Level-up buildings |
| Building demolition | Remove buildings |
| Build mode UI | Building category selection |

| Class | Description |
|-------|-------------|
| `LrgBuildingDef` | Building template/definition |
| `LrgBuildingInstance` | Placed building in world |
| `LrgPlacementSystem` | Placement logic and validation |
| `LrgBuildGrid` | Grid management |
| `LrgPlacementGhost` | Preview rendering |
| `LrgBuildingUI` | Build mode interface |

---

### 2.3 Vehicle/Driving System

**Module**: `src/vehicle/`

Many of your target games include driving elements.

| Feature | Description |
|---------|-------------|
| Vehicle controller | Arcade/sim driving physics |
| Wheel physics | Wheel colliders and suspension |
| Vehicle input | Steering, throttle, brake, handbrake |
| Vehicle cameras | Follow cam, hood cam, cockpit |
| Vehicle audio | Engine sounds, tire squeal, impacts |
| Traffic AI | NPC vehicle behavior |
| Road/path system | Spline-based roads |
| Vehicle damage | Visual and functional damage |

| Class | Description |
|-------|-------------|
| `LrgVehicle` | Base vehicle class |
| `LrgVehicleController` | Input to physics translation |
| `LrgWheel` | Wheel physics component |
| `LrgVehicleCamera` | Vehicle-specific camera modes |
| `LrgVehicleAudio` | Engine/tire sound management |
| `LrgTrafficAgent` | AI traffic participant |
| `LrgRoad` | Road segment definition |
| `LrgRoadNetwork` | Connected road system |

---

### 2.4 Idle Game Support

**Module**: `src/idle/` (or extend `src/economy/`)

For idle/incremental games.

| Feature | Description |
|---------|-------------|
| Offline progress | Calculate progress while game closed |
| Big number formatting | 1.5M, 2.3B, 4.7T, etc. |
| Prestige system | Reset with permanent bonuses |
| Auto-clicker/automation | Automated resource generation |
| Unlock trees | Progression unlock system |
| Milestone tracking | Achievement-like progression goals |
| Time dilation | Speed up simulation |

| Class | Description |
|-------|-------------|
| `LrgIdleCalculator` | Offline progress math |
| `LrgBigNumber` | Arbitrary precision numbers |
| `LrgPrestige` | Prestige layer logic |
| `LrgUnlockTree` | Unlock progression tree |
| `LrgMilestone` | Milestone definition |
| `LrgAutomation` | Auto-click/auto-buy logic |

---

## Phase 3: Commercial Polish

**Priority**: HIGH
**Description**: Features needed for professional game feel.

### 3.1 Particle System

**Module**: `src/particles/`

| Feature | Description |
|---------|-------------|
| Particle emitter | Spawn particles with properties |
| Particle properties | Life, velocity, color, size over time |
| Emission shapes | Point, circle, rectangle, cone, mesh |
| Particle forces | Gravity, wind, attractors, turbulence |
| Particle rendering | Billboard, stretched billboard, trail, mesh |
| GPU particles | Compute shader particles (optional) |
| Particle pooling | Object pooling for performance |

| Class | Description |
|-------|-------------|
| `LrgParticleEmitter` | Emission configuration |
| `LrgParticle` | Individual particle data |
| `LrgParticleSystem` | Update and rendering manager |
| `LrgParticleForce` | Force field definition |
| `LrgParticlePool` | Object pool |

---

### 3.2 Post-Processing Pipeline

**Module**: `src/postprocess/`

graylib supports shaders; this adds higher-level management.

| Effect | Description |
|--------|-------------|
| Bloom | Glow effect on bright areas |
| Color grading | LUT-based color adjustment |
| Vignette | Edge darkening |
| Motion blur | Camera motion blur |
| Depth of field | Focus blur |
| Screen shake | Camera shake effect |
| Colorblind filters | Accessibility color filters |
| FXAA/SMAA | Anti-aliasing |
| Film grain | Subtle noise overlay |

| Class | Description |
|-------|-------------|
| `LrgPostProcessor` | Effect pipeline manager |
| `LrgPostEffect` | Base effect class |
| `LrgBloom` | Bloom effect |
| `LrgColorGrade` | Color grading effect |
| `LrgVignette` | Vignette effect |
| `LrgMotionBlur` | Motion blur effect |
| `LrgDOF` | Depth of field effect |
| `LrgScreenShake` | Screen shake effect |

---

### 3.3 Animation State Machine

**Module**: `src/animation/`

Current `LrgAnimatorComponent` handles sprite animation. This adds 3D and complex animation.

| Feature | Description |
|---------|-------------|
| Skeletal animation | Bone-based animation |
| Animation blending | Smooth transitions between clips |
| Animation state machine | Complex state transitions |
| Animation events | Callbacks at keyframes |
| IK (Inverse Kinematics) | Foot placement, look-at, reach |
| Animation retargeting | Share animations between rigs |
| Animation layers | Blend multiple animations |
| Root motion | Extract movement from animation |

| Class | Description |
|-------|-------------|
| `LrgAnimationClip` | Animation data |
| `LrgAnimator` | Playback controller |
| `LrgAnimationStateMachine` | State machine |
| `LrgAnimationState` | Individual state |
| `LrgAnimationTransition` | State transition rules |
| `LrgSkeleton` | Skeletal hierarchy |
| `LrgBone` | Individual bone |
| `LrgIKSolver` | Inverse kinematics solver |
| `LrgAnimationLayer` | Animation layer |

---

### 3.4 Rich Text / Font Improvements

**Module**: `src/text/`

Enhance `LrgLabel` with advanced text features.

| Feature | Description |
|---------|-------------|
| Rich text | Inline formatting (bold, italic, color) |
| Text effects | Shake, wave, typewriter, rainbow |
| Font fallbacks | Unicode coverage via fallback fonts |
| SDF fonts | Scalable sharp text at any size |
| Text wrapping | Proper word wrap |
| Text alignment | Left/center/right/justify |
| Localized fonts | CJK, RTL, Arabic support |
| Text outlines | Stroke around text |

| Class | Description |
|-------|-------------|
| `LrgRichText` | Markup parsing and rendering |
| `LrgTextEffect` | Animated text effects |
| `LrgFontManager` | Font loading with fallbacks |
| `LrgSDFFont` | SDF font rendering |

---

### 3.5 Video Playback

**Module**: `src/video/`

For cutscenes, trailers, tutorials.

| Feature | Description |
|---------|-------------|
| Video decoder | Decode VP9/AV1/H.264 |
| Audio sync | Sync video with audio stream |
| Subtitle overlay | Render subtitles on video |
| Skip/pause controls | Player controls |
| Render to texture | In-world video displays |
| Looping | Loop video playback |

| Class | Description |
|-------|-------------|
| `LrgVideoPlayer` | Video playback controller |
| `LrgVideoTexture` | Render video to texture |
| `LrgVideoSubtitles` | Subtitle rendering |

**Dependencies**: FFmpeg/libav (LGPL)

**Note**: Valve recommends VP9 or AV1 for bandwidth efficiency.

---

## Phase 4: Complete Experience

**Priority**: MEDIUM
**Description**: Features for a polished, complete game.

### 4.1 Tweening/Easing Library

**Module**: `src/tween/`

| Feature | Description |
|---------|-------------|
| Property tweening | Animate any GObject property |
| Easing functions | ease-in, ease-out, ease-in-out, elastic, bounce, etc. |
| Tween sequences | Chain tweens together |
| Parallel tweens | Run multiple tweens simultaneously |
| Callbacks | On start, update, complete |
| Infinite loops | Ping-pong, restart |

| Class | Description |
|-------|-------------|
| `LrgTween` | Single property animation |
| `LrgTweenSequence` | Sequential tweens |
| `LrgTweenParallel` | Parallel tweens |
| `LrgEasing` | Easing function library |

---

### 4.2 Scene Transitions

**Module**: `src/transition/`

| Effect | Description |
|--------|-------------|
| Fade | Fade in/out to color |
| Wipe | Directional wipe |
| Dissolve | Shader-based dissolve |
| Slide | Slide scenes in/out |
| Zoom | Zoom in/out transition |
| Custom | User-defined shader transitions |

| Class | Description |
|-------|-------------|
| `LrgTransition` | Base transition class |
| `LrgFadeTransition` | Fade effect |
| `LrgWipeTransition` | Wipe effect |
| `LrgDissolveTransition` | Dissolve effect |
| `LrgTransitionManager` | Transition controller |

---

### 4.3 2D Trigger System

Extend world3d triggers to 2D.

| Feature | Description |
|---------|-------------|
| 2D area triggers | Rectangle, circle, polygon areas |
| Event scripting | YAML event definitions |
| Conditional triggers | State-based activation |
| Cooldowns | Trigger repeat rules |
| Enter/stay/exit | Trigger event types |

| Class | Description |
|-------|-------------|
| `LrgTrigger2D` | 2D trigger zone |
| `LrgTriggerEvent` | Trigger event definition |

---

### 4.4 Texture Atlas / Sprite Sheet Tools

| Feature | Description |
|---------|-------------|
| Atlas packing | Combine textures at build time |
| Runtime atlas | Dynamic batching |
| 9-slice/9-patch | Scalable UI sprites |
| Sprite sheet parsing | Load Aseprite, TexturePacker formats |
| Animation from sheet | Auto-detect animation frames |

| Class | Description |
|-------|-------------|
| `LrgTextureAtlas` | Packed texture atlas |
| `LrgAtlasRegion` | Region within atlas |
| `LrgNineSlice` | 9-slice sprite rendering |
| `LrgSpriteSheet` | Sprite sheet parser |

---

### 4.5 Tutorial System

**Module**: `src/tutorial/`

| Feature | Description |
|---------|-------------|
| Tutorial steps | Sequential instructions |
| Highlight system | Highlight UI elements |
| Input prompts | Show control hints (with correct glyphs) |
| Progress tracking | Skip completed tutorials |
| Tooltip arrows | Point to UI elements |
| Block input | Force player to follow tutorial |

| Class | Description |
|-------|-------------|
| `LrgTutorial` | Tutorial definition |
| `LrgTutorialStep` | Individual step |
| `LrgTutorialManager` | Tutorial controller |
| `LrgHighlight` | UI highlight effect |
| `LrgInputPrompt` | Control hint display |

---

### 4.6 Weather System

**Module**: `src/weather/`

| Feature | Description |
|---------|-------------|
| Rain | Particle rain with puddles |
| Snow | Particle snow with accumulation |
| Fog | Volumetric or simple fog |
| Wind | Particle/vegetation wind |
| Lightning | Lightning flash and sound |
| Day/night cycle | Time-based lighting changes |
| Weather audio | Ambient weather sounds |
| Weather transitions | Smooth weather changes |

| Class | Description |
|-------|-------------|
| `LrgWeather` | Weather state definition |
| `LrgWeatherManager` | Weather controller |
| `LrgDayNightCycle` | Time-of-day system |
| `LrgRain` | Rain effect |
| `LrgSnow` | Snow effect |
| `LrgFog` | Fog effect |

---

### 4.7 Lighting Improvements

| Feature | Description |
|---------|-------------|
| 2D lighting | Point/spot lights for 2D games |
| Shadow mapping | Dynamic shadows |
| Light culling | Performance optimization |
| Baked lighting | Pre-computed lightmaps |
| Light probes | Ambient lighting sampling |
| Emissive materials | Self-illuminating surfaces |

| Class | Description |
|-------|-------------|
| `LrgLight2D` | 2D light source |
| `LrgShadowMap` | Shadow map renderer |
| `LrgLightmap` | Baked lightmap |
| `LrgLightProbe` | Light probe |

---

## Phase 5: Enhancements

**Priority**: LOW
**Description**: Nice-to-have features for specific needs.

### 5.1 Analytics/Telemetry

| Feature | Description |
|---------|-------------|
| Event tracking | Track player actions |
| Session tracking | Play time, retention |
| Custom metrics | Game-specific data |
| GDPR compliance | Consent management |
| Privacy-first | No PII collection |

| Class | Description |
|-------|-------------|
| `LrgAnalytics` | Analytics manager |
| `LrgAnalyticsEvent` | Event definition |
| `LrgConsent` | GDPR consent tracking |

---

### 5.2 Local Achievement System

Complements Steam Achievements for offline play.

| Feature | Description |
|---------|-------------|
| Achievement definitions | Define achievements |
| Progress tracking | Cumulative achievements |
| Notification UI | Achievement unlock popups |
| Statistics | Local stat tracking |

| Class | Description |
|-------|-------------|
| `LrgAchievement` | Achievement definition |
| `LrgAchievementManager` | Achievement tracking |
| `LrgAchievementNotification` | Popup UI |

---

### 5.3 Photo Mode

| Feature | Description |
|---------|-------------|
| Screenshot capture | Save PNG/JPG |
| Free camera | Detached camera controls |
| Filters | Post-process filters |
| Hide UI | Toggle UI visibility |
| Focus point | Depth of field focus |
| Steam integration | Upload to Steam |

| Class | Description |
|-------|-------------|
| `LrgPhotoMode` | Photo mode controller |
| `LrgScreenshot` | Screenshot capture |

---

### 5.4 Steam Workshop Integration

Your mod system provides the runtime; this adds Steam distribution.

| Feature | Description |
|---------|-------------|
| Mod upload | Publish mods to Workshop |
| Mod download | Subscribe and download mods |
| Mod management | Enable/disable/order mods |
| Mod dependencies | Handle mod dependencies |

| Class | Description |
|-------|-------------|
| `LrgWorkshopItem` | Workshop item wrapper |
| `LrgWorkshopManager` | Workshop integration |

---

### 5.5 Demo Support

| Feature | Description |
|---------|-------------|
| Content gating | Lock full game content |
| Demo save conversion | Upgrade save to full game |
| Demo ending | Purchase prompt at end |
| Time limit | Optional time-limited demo |

| Class | Description |
|-------|-------------|
| `LrgDemoManager` | Demo mode controller |

---

### 5.6 VR Support (if needed)

| Feature | Description |
|---------|-------------|
| SteamVR integration | VR rendering pipeline |
| VR input | Motion controller input |
| VR UI | World-space UI rendering |
| Comfort options | Vignette, snap turn, etc. |

**Dependencies**: OpenVR SDK

---

## Platform-Specific Requirements

### Linux/Steam Deck

| Feature | Status | Notes |
|---------|--------|-------|
| Native Linux build | ✅ Have | GLib/raylib work on Linux |
| Steam Deck testing | ❌ Missing | Verify on actual hardware |
| Controller glyphs | ❌ Missing | Show correct button icons |
| Suspend/resume | ❌ Missing | Handle sleep gracefully |

### Windows

| Feature | Status | Notes |
|---------|--------|-------|
| Windows build | ⚠️ Partial | Needs cross-compilation setup |
| Windows installer | ❌ Missing | NSIS or similar |
| Visual C++ redist | ❌ Missing | Bundle or auto-install |

---

## External Dependencies

| Library | Purpose | License | Phase |
|---------|---------|---------|-------|
| Steamworks SDK | Steam integration | Proprietary (free) | 1 |
| FFmpeg/libav | Video playback | LGPL | 3 |
| espeak/speech-dispatcher | Screen reader | GPL | 1 |
| BugSplat SDK (optional) | Crash reporting | Commercial (free tier) | 1 |

---

## Estimates

| Category | New Modules | Source Files | Test Files |
|----------|-------------|--------------|------------|
| Phase 1 | 5-6 | 30-40 | 6-8 |
| Phase 2 | 4 | 25-35 | 4-6 |
| Phase 3 | 5 | 30-40 | 5-7 |
| Phase 4 | 7 | 25-35 | 5-7 |
| Phase 5 | 5-6 | 15-25 | 3-5 |
| **Total** | **26-29** | **125-175** | **23-33** |

---

## References

- [Steamworks Documentation](https://partner.steamgames.com/doc/home)
- [Steamworks Features](https://partner.steamgames.com/doc/features)
- [Steam Deck and Proton](https://partner.steamgames.com/doc/steamdeck/proton)
- [Steam Game Publishing Guide 2025](https://generalistprogrammer.com/tutorials/steam-game-publishing-complete-developer-guide-2025)
- [Game Accessibility Guide](https://generalistprogrammer.com/game-accessibility)
- [CVAA Video Game Requirements](https://www.3playmedia.com/blog/the-cvaa-video-game-accessibility/)
- [European Accessibility Act](https://ec.europa.eu/social/main.jsp?catId=1202)
- [Game Settings Checklist](https://www.gamedeveloper.com/design/create-better-game-settings-options-handy-checklist-)
- [BugSplat for Game Development](https://www.bugsplat.com/for/game-development/)
- [ProtonDB](https://www.protondb.com/)
