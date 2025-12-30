# Mod System Module

## Overview

The Mod System provides complete plugin infrastructure for extending Libregnum with custom content and features. It handles mod discovery, dependency resolution, loading/unloading, and content provider interfaces.

## Key Components

### Core Types

- **LrgModManager** - Central mod lifecycle management and loading orchestration
- **LrgModLoader** - Filesystem discovery and manifest parsing
- **LrgMod** - Individual mod representation and state management
- **LrgModManifest** (+ LrgModDependency) - Mod metadata and dependency definitions
- **LrgModable** (interface) - Base interface for modable objects

### Provider Interfaces

- **LrgEntityProvider** - Provides custom entity types
- **LrgItemProvider** - Provides item definitions
- **LrgSceneProvider** - Provides game scenes
- **LrgDialogProvider** - Provides dialog trees
- **LrgQuestProvider** - Provides quest definitions
- **LrgAIProvider** - Provides AI behavior tree nodes
- **LrgCommandProvider** - Provides console commands
- **LrgLocaleProvider** - Provides localization data
- **LrgConsoleCommand** (boxed) - Console command definitions

## Architecture

### Mod Lifecycle

1. **Discovery** - Loader scans search paths for mod manifests
2. **Validation** - Manifests parsed, dependencies checked
3. **Ordering** - Load order computed (dependencies first, priority second)
4. **Loading** - Mods loaded in order, providers registered
5. **Runtime** - Mods provide content to engine systems
6. **Unloading** - Mods shut down in reverse order

### Manifest Format

Mods are discovered via YAML manifest files:

```yaml
id: my_mod
name: My Custom Mod
version: 1.0.0
author: Your Name
description: A great mod that adds stuff
type: content              # content, script, native
priority: normal           # core, important, normal, optional
dependencies:
  - id: base_game
    min_version: 1.0.0
    optional: false
data_path: data/
entry_point: mod.py       # For script/native mods
load_after:
  - other_mod_1
load_before:
  - another_mod
```

### Provider Pattern

Mods implement provider interfaces to contribute content:

```c
/* Mod implements LrgEntityProvider */
GList *
my_mod_get_entity_types(LrgEntityProvider *self)
{
    GList *types = NULL;
    types = g_list_append(types, GINT_TO_POINTER(MY_TYPE_CUSTOM_PLAYER));
    types = g_list_append(types, GINT_TO_POINTER(MY_TYPE_CUSTOM_ENEMY));
    return types;
}
```

## Quick Start

### Setting Up Mod Manager

```c
g_autoptr(LrgModManager) mod_mgr = lrg_mod_manager_get_default();
lrg_mod_manager_add_search_path(mod_mgr, "/usr/share/libregnum/mods");
lrg_mod_manager_add_search_path(mod_mgr, "~/.local/share/libregnum/mods");

/* Discover and load mods */
g_autoptr(GError) error = NULL;
guint discovered = lrg_mod_manager_discover(mod_mgr, &error);
g_print("Discovered %u mods\n", discovered);

if (!lrg_mod_manager_load_all(mod_mgr, &error)) {
    g_print("Error loading mods: %s\n", error->message);
    return FALSE;
}

g_print("Mods loaded successfully\n");
```

### Creating a Content Mod

```c
/* Implement mod manifest */
g_autoptr(LrgModManifest) manifest = lrg_mod_manifest_new("my_content_mod");
lrg_mod_manifest_set_name(manifest, "My Content Mod");
lrg_mod_manifest_set_version(manifest, "1.0.0");
lrg_mod_manifest_set_author(manifest, "Your Name");
lrg_mod_manifest_add_dependency("base_game", "1.0.0", FALSE);
lrg_mod_manifest_set_data_path(manifest, "data/");

/* Create mod from manifest */
g_autoptr(LrgMod) mod = lrg_mod_new(manifest, "/path/to/mod");

/* Load and register with manager */
g_autoptr(GError) error = NULL;
if (lrg_mod_load(mod, &error))
    g_print("Mod loaded: %s\n", lrg_mod_get_id(mod));
else
    g_print("Load failed: %s\n", error->message);
```

### Collecting Content from Providers

```c
/* Get all entity types from loaded mods */
g_autoptr(GList) entity_types = lrg_mod_manager_collect_entity_types(mod_mgr);
for (GList *l = entity_types; l; l = l->next) {
    GType entity_type = GPOINTER_TO_INT(l->data);
    const gchar *name = g_type_name(entity_type);
    g_print("Entity type: %s\n", name);
}

/* Get all items */
g_autoptr(GList) item_defs = lrg_mod_manager_collect_item_defs(mod_mgr);
for (GList *l = item_defs; l; l = l->next) {
    LrgItemDef *item = l->data;
    /* Register item in inventory system */
}
```

## Module Documentation

- [LrgModManager](mod-manager.md) - Central mod management
- [LrgModLoader](mod-loader.md) - Filesystem discovery
- [LrgMod](mod.md) - Individual mod representation
- [LrgModManifest](mod-manifest.md) - Mod metadata
- [LrgModable](modable.md) - Base interface
- [Provider Interfaces](providers.md) - Content provider interfaces

## Examples

- [Modding Guide](../../examples/modding-guide.md) - Complete mod creation walkthrough
- [DLC Store Demo](../../examples/dlc-store.md) - DLC ownership, trials, and store integration
- [Native DLC Example](../../examples/native-dlc.md) - GModule-based native code DLCs

## Mod States

```
DISCOVERED -> ENABLED -> LOADING -> LOADED
              DISABLED                  |
                                        v
                                    UNLOADING -> UNLOADED
                  ERROR <-- (any state on failure)
```

## Common Patterns

### Conditional Loading

```c
/* Load only required mods */
g_autoptr(GPtrArray) mods = lrg_mod_manager_get_mods(mod_mgr);
for (guint i = 0; i < mods->len; i++) {
    LrgMod *mod = g_ptr_array_index(mods, i);
    LrgModManifest *manifest = lrg_mod_get_manifest(mod);
    LrgModType type = lrg_mod_manifest_get_mod_type(manifest);

    if (type != LRG_MOD_TYPE_CORE) {
        /* Disable optional content mods */
        lrg_mod_manager_disable_mod(mod_mgr, lrg_mod_get_id(mod));
    }
}
```

### Resource Resolution

```c
/* Load texture from any mod (mods override in reverse order) */
gchar *texture_path = lrg_mod_manager_resolve_path(mod_mgr, "textures/wood.png");
if (texture_path) {
    g_autoptr(GrlTexture) texture = load_texture(texture_path);
    g_free(texture_path);
}
```

### Dependency Checking

```c
/* Verify all dependencies before loading */
g_autoptr(GError) error = NULL;
if (!lrg_mod_manager_check_dependencies(mod_mgr, mod, &error)) {
    g_print("Dependency error: %s\n", error->message);
    return FALSE;
}
```

## See Also

- [Glossary](../../reference/glossary.md) - Terminology
- [Error Codes](../../reference/error-codes.md) - Mod system errors
