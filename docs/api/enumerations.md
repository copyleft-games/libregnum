---
title: Enumerations Index
---

# Libregnum Enumerations

Complete index of all enumerations in Libregnum.

> **[Home](../index.md)** > API > Enumerations

## Core Module

### Engine

- **LrgEngineState** - Engine lifecycle states
  - UNINITIALIZED, INITIALIZING, RUNNING, PAUSED, SHUTTING_DOWN, TERMINATED

### Error Domains

- **LrgEngineError** - Engine errors
  - FAILED, INIT, STATE

- **LrgDataLoaderError** - Data loader errors
  - FAILED, IO, PARSE, TYPE, PROPERTY

- **LrgAssetManagerError** - Asset manager errors
  - NOT_FOUND, LOAD_FAILED, INVALID_TYPE

## Phase 1: Game Systems

### Input

- **LrgInputBindingType** - Type of input binding
  - KEYBOARD, MOUSE_BUTTON, GAMEPAD_BUTTON, GAMEPAD_AXIS

- **LrgInputModifiers** - Input modifier flags
  - NONE, SHIFT, CTRL, ALT

### UI

- **LrgTextAlignment** - Text alignment
  - LEFT, CENTER, RIGHT

- **LrgOrientation** - Widget orientation
  - HORIZONTAL, VERTICAL

- **LrgImageScaleMode** - Image scaling
  - FIT, FILL, STRETCH, TILE

- **LrgUIEventType** - UI event types
  - NONE, MOUSE_MOVE, MOUSE_BUTTON_DOWN, MOUSE_BUTTON_UP, KEY_DOWN, KEY_UP, SCROLL, FOCUS_IN, FOCUS_OUT, TEXT_INPUT

### Tilemap

- **LrgTileProperty** - Tile property flags
  - NONE, SOLID, ANIMATED, HAZARD

## Phase 2: Content

### Dialog

- **LrgDialogError** - Dialog system errors
  - FAILED, INVALID_NODE, NO_TREE, CONDITION

### Inventory

- **LrgItemType** - Item type
  - GENERIC, WEAPON, ARMOR, CONSUMABLE, QUEST, MATERIAL

- **LrgEquipmentSlot** - Equipment slots
  - HEAD, CHEST, LEGS, FEET, HANDS, WEAPON, OFFHAND, ACCESSORY

### Quest

- **LrgQuestState** - Quest state
  - AVAILABLE, ACTIVE, COMPLETE, FAILED

- **LrgQuestObjectiveType** - Objective type
  - KILL, COLLECT, INTERACT, REACH, ESCORT, CUSTOM

### Save

- **LrgSaveError** - Save system errors
  - FAILED, IO, VERSION_MISMATCH, CORRUPT, NOT_FOUND

## Phase 3: Advanced

### AI

- **LrgBTStatus** - Behavior tree node status
  - INVALID, SUCCESS, FAILURE, RUNNING

- **LrgBTParallelPolicy** - Parallel composite policy
  - REQUIRE_ONE, REQUIRE_ALL

- **LrgBlackboardValueType** - Blackboard value types
  - INT, FLOAT, BOOL, STRING, OBJECT, VECTOR2

### Physics

- **LrgRigidBodyType** - Rigid body type
  - DYNAMIC, KINEMATIC, STATIC

- **LrgForceMode** - Force application mode
  - FORCE, IMPULSE, ACCELERATION, VELOCITY_CHANGE

- **LrgCollisionShape** - Collision shape type
  - BOX, CIRCLE, CAPSULE, POLYGON

### Pathfinding

- **LrgPathfindingError** - Pathfinding errors
  - FAILED, NO_PATH, OUT_OF_BOUNDS, BLOCKED, NO_GRID, INVALID_START, INVALID_GOAL

- **LrgNavCellFlags** - Navigation cell properties
  - NONE, WALKABLE, BLOCKED

- **LrgPathSmoothingMode** - Path smoothing algorithm
  - NONE, SIMPLE, BEZIER

### I18N

- **LrgI18nError** - Localization errors
  - FAILED, NOT_FOUND, LOCALE_NOT_FOUND, PARSE

- **LrgPluralForm** - Plural forms (CLDR)
  - ZERO, ONE, TWO, FEW, MANY, OTHER

### Networking

- **LrgNetError** - Networking errors
  - FAILED, CONNECTION_FAILED, CONNECTION_CLOSED, MESSAGE_INVALID, TIMEOUT, ALREADY_CONNECTED, NOT_CONNECTED, SEND_FAILED

- **LrgNetPeerState** - Network peer state
  - DISCONNECTED, CONNECTING, CONNECTED, DISCONNECTING

- **LrgNetMessageType** - Network message type
  - HANDSHAKE, DATA, PING, PONG, DISCONNECT

### 3D World

- **LrgSpawnType** - Spawn point type
  - PLAYER, ENEMY, NPC, ITEM, GENERIC

- **LrgTriggerType** - Trigger event type
  - ENTER, EXIT, INTERACT, PROXIMITY

- **LrgOctreeNodeType** - Octree node type
  - EMPTY, LEAF, BRANCH

## Phase 4: Engine Tools

### Debug

- **LrgDebugError** - Debug system errors
  - FAILED, COMMAND_NOT_FOUND, INVALID_ARGS

- **LrgDebugOverlayFlags** - Debug overlay display flags
  - NONE, FPS, FRAME_TIME, MEMORY, ENTITIES, PHYSICS, COLLIDERS, PROFILER, CUSTOM, ALL

- **LrgProfilerSectionType** - Profiler section type
  - UPDATE, PHYSICS, RENDER, AI, AUDIO, CUSTOM

### Mod System

- **LrgModError** - Mod system errors
  - FAILED, NOT_FOUND, LOAD_FAILED, INVALID_MANIFEST, MISSING_DEPENDENCY, VERSION, CIRCULAR

- **LrgModState** - Mod state
  - UNLOADED, DISCOVERED, LOADING, LOADED, FAILED, DISABLED

- **LrgModType** - Mod type
  - DATA, SCRIPT, NATIVE

- **LrgModPriority** - Mod load priority
  - LOWEST, LOW, NORMAL, HIGH, HIGHEST

## See Also

- [API Classes Index](classes.md)
- [API Interfaces Index](interfaces.md)
- [API Boxed Types Index](boxed-types.md)
- [Core Module](../modules/core/index.md)
