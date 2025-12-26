---
title: Classes Index
---

# Libregnum Classes

Complete index of all classes and derivable types in Libregnum.

> **[Home](../index.md)** > API > Classes

## Core Module (Phase 0 - Complete)

### Engine

- **[LrgEngine](../modules/core/engine.md)** - Central engine singleton (Derivable)

### Type System

- **[LrgRegistry](../modules/core/registry.md)** - Type registry (Final)

### Data Loading

- **[LrgDataLoader](../modules/core/data-loader.md)** - YAML deserialization (Final)

### Asset Management

- **[LrgAssetManager](../modules/core/asset-manager.md)** - Asset caching and loading (Derivable)

## Planned: ECS Module (Phase 1)

- LrgGameObject - Game entity
- LrgComponent - Base component (Derivable)
- LrgTransformComponent - Position/rotation/scale
- LrgSpriteComponent - Graphics rendering
- LrgColliderComponent - Collision detection
- LrgAnimatorComponent - Animation playback
- LrgWorld - Entity container

## Planned: Input Module (Phase 1)

- LrgInputMap - Input action mapping
- LrgInputAction - Input action
- LrgInputBinding - Input key binding

## Planned: UI Module (Phase 1)

- LrgWidget - Base widget (Derivable)
- LrgContainer - Widget container
- LrgLabel - Text label
- LrgButton - Clickable button
- LrgPanel - Panel widget
- LrgCheckbox - Checkbox control
- LrgSlider - Slider control
- LrgProgressBar - Progress bar
- LrgTextInput - Text input field
- LrgImage - Image widget
- LrgVBox - Vertical layout
- LrgHBox - Horizontal layout
- LrgGrid - Grid layout
- LrgCanvas - Custom drawing canvas
- LrgTheme - UI theme

## See Also

- [API Interfaces Index](interfaces.md)
- [API Enumerations Index](enumerations.md)
- [Core Module](../modules/core/index.md)
- [Module Index](../modules/index.md)
