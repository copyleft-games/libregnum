---
title: Core Concepts
---

# Core Concepts

Understand the fundamental ideas behind Libregnum's design and architecture.

> **[Home](../index.md)** > Concepts

## Essential Concepts

### [Engine Lifecycle](engine-lifecycle.md)

How the engine starts, runs, and shuts down. Understand the state machine and lifecycle events.

- Engine states (UNINITIALIZED, INITIALIZING, RUNNING, PAUSED, SHUTTING_DOWN, TERMINATED)
- Startup/shutdown process
- Game loop integration
- Signal emissions at each stage

### [Type Registry](type-registry.md)

Data-driven type mapping from string names to GTypes. The foundation of Libregnum's data-driven design.

- Registering types for YAML deserialization
- Creating objects by name
- Type lookup and enumeration
- Mod overrides

### [Data Loading](data-loading.md)

YAML-based object deserialization using the Registry and DataLoader.

- YAML structure and type fields
- Single file vs batch loading
- Synchronous and asynchronous loading
- Error handling during loading

### [Error Handling](error-handling.md)

GError patterns and error recovery throughout Libregnum.

- Error domains and codes
- Error checking with GError
- Specific error handling
- Recovery strategies

## Advanced Concepts

### [ECS Pattern](ecs-pattern.md) *(Phase 1)*

Entity-Component-System architecture for flexible and composable gameplay.

- Game objects and components
- Component-based design
- World and entity management
- Systems updating entities

### [Modding](modding.md) *(Phase 4)*

Extend the engine with mods: data mods, script mods, and native plugins.

- Mod manifests and dependencies
- Asset overlay with mods
- Type overrides with mods
- Mod loading and management

## Related Topics

- **[Architecture Overview](../architecture.md)** - System design and patterns
- **[Module Documentation](../modules/index.md)** - All game systems
- **[API Reference](../api/classes.md)** - Complete type documentation

## Quick Reference

| Concept | Purpose | Key Type |
|---------|---------|----------|
| Engine Lifecycle | Manage engine state | `LrgEngine` |
| Type Registry | Map string names to types | `LrgRegistry` |
| Data Loading | Deserialize YAML to objects | `LrgDataLoader` |
| Error Handling | Consistent error reporting | `GError` |
| ECS | Flexible entity composition | `LrgGameObject` |
| Modding | Extend game with mods | `LrgModManager` |
