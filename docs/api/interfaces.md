---
title: Interfaces Index
---

# Libregnum Interfaces

Index of all interfaces (abstract types) in Libregnum.

> **[Home](../index.md)** > API > Interfaces

## Core Module

*No interfaces in Core module*

## Planned Interfaces

### Saveable Interface (Phase 2)

**LrgSaveable** - Objects that can be saved/loaded

Methods:
- `save()` - Serialize object
- `load()` - Deserialize object

Used by: Any object that needs persistence

### Modable Interface (Phase 4)

**LrgModable** - Objects that can be extended by mods

Methods:
- `on_mod_load()` - Called when mod loads
- `on_mod_unload()` - Called when mod unloads

Used by: Engine, systems, objects

### Provider Interfaces (Extensibility)

**LrgEntityProvider** - Provides custom entities
**LrgItemProvider** - Provides custom items
**LrgSceneProvider** - Provides custom scenes
**LrgDialogProvider** - Provides custom dialogues
**LrgQuestProvider** - Provides custom quests
**LrgAIProvider** - Provides custom AI
**LrgCommandProvider** - Provides custom commands
**LrgLocaleProvider** - Provides custom locales

## See Also

- [API Classes Index](classes.md)
- [API Enumerations Index](enumerations.md)
- [Core Module](../modules/core/index.md)
