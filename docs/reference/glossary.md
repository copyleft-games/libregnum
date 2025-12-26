# Glossary

Key terminology used throughout Libregnum documentation and code.

## Architecture Concepts

**ECS** (Entity Component System)
A software architecture pattern where game objects (entities) are composed of reusable components representing functionality, rather than using class inheritance.

**GameObject / Entity**
A discrete object in the game world (player character, enemy, item, etc.). In Libregnum, entities are GObject instances registered with the type registry.

**Component**
A reusable piece of functionality that can be attached to entities. Examples: Health, Inventory, Animation, Physics.

**World / Level**
The complete game environment containing all entities, geometry, and interactive elements. In Libregnum, represented by `LrgLevel3D` for 3D levels.

**Scene**
A complete game level or environment state. Contains all entities and data needed for that portion of the game. Managed by graylib's `GrlScene`.

## Spatial Concepts

**Octree**
A tree data structure that recursively subdivides 3D space into 8 cubic regions. Used for spatial partitioning and efficient collision/visibility queries.

**Bounding Box (AABB)**
Axis-Aligned Bounding Box. The smallest box with sides parallel to axes that fully contains an object. Used for collision detection and spatial queries.

**Sector**
A convex region of space in a portal-based visibility system. Sectors are connected by portals to form the level topology.

**Portal**
An opening between two sectors that allows visibility traversal. Used for occlusion culling in indoor environments.

**Visibility Culling**
The process of determining which objects are visible from the camera position to avoid rendering hidden geometry. Portal systems use visibility culling.

**Occlusion Culling**
Technique for not rendering objects that are occluded (hidden) by other objects, improving performance.

## Game Systems

**Trigger**
An interactive volume that fires events when an entity enters or interacts with it. Used for quest objectives, hazards, level transitions.

**Spawn Point**
A location where entities are created during gameplay. Can specify entity type and custom properties.

**Dialog Tree**
A branching conversation structure used for NPC interactions.

**Quest**
A gameplay objective given to the player, typically with multiple steps and rewards.

**Item**
A collectable object (equipment, consumable, etc.) that can be stored in inventory.

**NPC** (Non-Player Character)
A character controlled by AI rather than the player.

**Behavior Tree**
A hierarchical decision-making structure used for AI control. Composed of composite nodes (selectors, sequences) and leaf nodes (actions, conditions).

## GObject Concepts

**GObject**
The base object type in GLib providing object-oriented features: properties, signals, introspection.

**GType**
The type system in GLib. Every class is registered with a unique GType identifier used for polymorphism and introspection.

**Property**
Named attribute of a GObject with a specific type and access flags (read/write). Can emit notifications on change.

**Signal**
Event mechanism in GObject. Handlers can connect to signals to be notified when events occur.

**Interface**
A contract defining a set of virtual methods that implementing types must provide.

**Boxed Type**
A value type (immutable data structure) wrapped for use with GObject introspection. Examples: `LrgBoundingBox3D`, `LrgSector`.

**Final Type**
A GObject type that cannot be subclassed.

**Derivable Type**
A GObject type that can be subclassed by user code.

## Mod System Concepts

**Mod / Plugin**
A package of content or code that extends the game. Can provide entities, items, dialogs, scripts, etc.

**Manifest**
Metadata about a mod including ID, version, dependencies, and load order preferences. Usually in YAML format.

**Dependency**
A requirement that another mod or version be present before this mod can load.

**Load Order**
The sequence in which mods are initialized. Computed based on dependencies and priority.

**Provider**
An interface that mods implement to contribute specific types of content (entities, items, dialogs, etc.).

**Saveable / Serializable**
An object that can be converted to/from a data format (YAML, JSON) for persistence.

**Registry**
A central type registry that maps type names to GTypes. Mods register their custom types here.

## Gameplay Concepts

**Difficulty**
Game setting affecting challenge level, enemy strength, resource availability.

**Loot**
Items dropped by defeated enemies or found in containers.

**Faction**
A group or organization that NPCs belong to, affecting relationships and interactions.

**Navigation Grid**
A discretized representation of the game world used for pathfinding.

**Pathfinding / Pathfinding Algorithm**
Computation for determining movement path from point A to point B while avoiding obstacles. Common algorithms: A*, Dijkstra.

**Animation**
Sequence of frames or skeletal movements that change an object's appearance over time.

**Particle System**
Rendering system for many small objects (particles) to create effects like fire, smoke, water.

## Data Concepts

**YAML**
Human-readable data serialization format used for mod manifests and data files. Hierarchical with indentation-based structure.

**Serialization**
Converting objects to data formats for storage or transmission.

**Deserialization**
Reconstructing objects from serialized data.

**Schema**
The structure and types of data in a document.

## Enumerations

**LrgSpawnType**
Categories of spawn points: PLAYER, ENEMY, ITEM, NPC, PROJECTILE, PICKUP, DECORATION, LIGHT.

**LrgTriggerType**
Trigger activation types: VOLUME (enter), INTERACTION (player action), DAMAGE (hazard), WATER, PORTAL (level transition), SCRIPT (custom).

**LrgModType**
Mod categories: CORE (base game), CONTENT (items/enemies), SCRIPT (scripted), NATIVE (compiled code).

**LrgModPriority**
Load priority ordering: CORE (first), IMPORTANT, NORMAL, OPTIONAL (last).

**LrgModState**
Mod lifecycle states: DISCOVERED, LOADING, LOADED, UNLOADING, UNLOADED, ERROR.

## See Also

- [Error Codes](error-codes.md) - Error enumerations
- [Signals](signals.md) - Event reference
- [Properties](properties.md) - Object properties
