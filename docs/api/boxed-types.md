---
title: Boxed Types Index
---

# Libregnum Boxed Types

Index of all boxed types (lightweight value types) in Libregnum.

> **[Home](../index.md)** > API > Boxed Types

Boxed types are reference-counted value types used for small immutable data structures.

## Core Module

*No boxed types in Core module*

## Planned Boxed Types

### Navigation (Phase 3)

- **LrgNavCell** - Navigation grid cell
- **LrgPath** - Path waypoints

### Dialogs (Phase 2)

- **LrgDialogResponse** - Dialog choice response

### Quests (Phase 2)

- **LrgQuestObjective** - Quest objective

### Networking (Phase 3)

- **LrgNetMessage** - Network message

### 3D World (Phase 3)

- **LrgBoundingBox3D** - 3D bounding box
- **LrgSpawnPoint3D** - 3D spawn point
- **LrgTrigger3D** - 3D trigger volume
- **LrgPortal** - Portal between sectors
- **LrgSector** - 3D sector

### Mod System (Phase 4)

- **LrgSemver** - Semantic version (major.minor.patch)
- **LrgConsoleCommand** - Console command

## Characteristics

Boxed types are:
- **Value semantics** - Copied on assignment
- **Reference counted** - Memory managed automatically
- **Immutable** - Cannot be modified after creation
- **Small** - Fit in cache lines
- **Fast** - No indirection or heap allocation

## See Also

- [API Classes Index](classes.md)
- [API Interfaces Index](interfaces.md)
- [API Enumerations Index](enumerations.md)
- [Core Module](../modules/core/index.md)
