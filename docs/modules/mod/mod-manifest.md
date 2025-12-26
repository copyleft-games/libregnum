# LrgModManifest

## Overview

`LrgModManifest` contains metadata about a mod including ID, version, dependencies, and load order preferences. Manifests are loaded from YAML files.

## Type Information

- **Type Name**: `LrgModManifest`
- **Type ID**: `LRG_TYPE_MOD_MANIFEST`
- **Base Class**: `GObject`
- **Final Type**: Yes

## Boxed Type: LrgModDependency

### lrg_mod_dependency_new

```c
LrgModDependency *lrg_mod_dependency_new(const gchar *mod_id, const gchar *min_version, gboolean optional);
```

Creates a dependency specification.

### lrg_mod_dependency_get_mod_id

```c
const gchar *lrg_mod_dependency_get_mod_id(const LrgModDependency *self);
```

Gets the required mod ID.

### lrg_mod_dependency_get_min_version

```c
const gchar *lrg_mod_dependency_get_min_version(const LrgModDependency *self);
```

Gets minimum required version (nullable).

### lrg_mod_dependency_is_optional

```c
gboolean lrg_mod_dependency_is_optional(const LrgModDependency *self);
```

Checks if this dependency is optional.

## Construction

### lrg_mod_manifest_new

```c
LrgModManifest *lrg_mod_manifest_new(const gchar *mod_id);
```

Creates a new manifest with the given ID.

**Example:**
```c
g_autoptr(LrgModManifest) manifest = lrg_mod_manifest_new("content_pack_1");
```

### lrg_mod_manifest_new_from_file

```c
LrgModManifest *lrg_mod_manifest_new_from_file(const gchar *path, GError **error);
```

Loads a manifest from a YAML file.

**Returns:** (transfer full) (nullable) Manifest, or NULL on error

**Example:**
```c
g_autoptr(GError) error = NULL;
g_autoptr(LrgModManifest) manifest = lrg_mod_manifest_new_from_file("mods/my_mod/mod.yaml", &error);
if (!manifest)
    g_print("Parse error: %s\n", error->message);
```

## Identity

### lrg_mod_manifest_get_id

```c
const gchar *lrg_mod_manifest_get_id(LrgModManifest *self);
```

Gets the unique mod identifier (immutable).

### lrg_mod_manifest_get_name / set_name

```c
const gchar *lrg_mod_manifest_get_name(LrgModManifest *self);
void lrg_mod_manifest_set_name(LrgModManifest *self, const gchar *name);
```

Gets/sets display name.

### lrg_mod_manifest_get_version / set_version

```c
const gchar *lrg_mod_manifest_get_version(LrgModManifest *self);
void lrg_mod_manifest_set_version(LrgModManifest *self, const gchar *version);
```

Gets/sets version string (e.g., "1.2.3").

### lrg_mod_manifest_get_description / set_description

```c
const gchar *lrg_mod_manifest_get_description(LrgModManifest *self);
void lrg_mod_manifest_set_description(LrgModManifest *self, const gchar *description);
```

Gets/sets description text.

### lrg_mod_manifest_get_author / set_author

```c
const gchar *lrg_mod_manifest_get_author(LrgModManifest *self);
void lrg_mod_manifest_set_author(LrgModManifest *self, const gchar *author);
```

Gets/sets author name.

## Type and Priority

### lrg_mod_manifest_get_mod_type / set_mod_type

```c
LrgModType lrg_mod_manifest_get_mod_type(LrgModManifest *self);
void lrg_mod_manifest_set_mod_type(LrgModManifest *self, LrgModType type);
```

Gets/sets mod type.

**Types:**
- `LRG_MOD_TYPE_CORE` - Core game content
- `LRG_MOD_TYPE_CONTENT` - Additional content (items, enemies, etc.)
- `LRG_MOD_TYPE_SCRIPT` - Scripted mod
- `LRG_MOD_TYPE_NATIVE` - Native compiled mod

### lrg_mod_manifest_get_priority / set_priority

```c
LrgModPriority lrg_mod_manifest_get_priority(LrgModManifest *self);
void lrg_mod_manifest_set_priority(LrgModManifest *self, LrgModPriority priority);
```

Gets/sets load priority.

**Priorities:**
- `LRG_MOD_PRIORITY_CORE` - Load first
- `LRG_MOD_PRIORITY_IMPORTANT` - High priority
- `LRG_MOD_PRIORITY_NORMAL` - Default
- `LRG_MOD_PRIORITY_OPTIONAL` - Low priority

## Dependencies

### lrg_mod_manifest_get_dependencies

```c
GPtrArray *lrg_mod_manifest_get_dependencies(LrgModManifest *self);
```

Gets all dependencies.

**Returns:** (transfer none) (element-type LrgModDependency) Dependencies

### lrg_mod_manifest_add_dependency

```c
void lrg_mod_manifest_add_dependency(LrgModManifest *self, const gchar *mod_id,
                                     const gchar *min_version, gboolean optional);
```

Adds a dependency on another mod.

### lrg_mod_manifest_has_dependency

```c
gboolean lrg_mod_manifest_has_dependency(LrgModManifest *self, const gchar *mod_id);
```

Checks if mod depends on another.

## Load Order

### lrg_mod_manifest_get_load_after / add_load_after

```c
GPtrArray *lrg_mod_manifest_get_load_after(LrgModManifest *self);
void lrg_mod_manifest_add_load_after(LrgModManifest *self, const gchar *mod_id);
```

Specifies mods that should load before this one.

### lrg_mod_manifest_get_load_before / add_load_before

```c
GPtrArray *lrg_mod_manifest_get_load_before(LrgModManifest *self);
void lrg_mod_manifest_add_load_before(LrgModManifest *self, const gchar *mod_id);
```

Specifies mods that should load after this one.

## Paths

### lrg_mod_manifest_get_data_path / set_data_path

```c
const gchar *lrg_mod_manifest_get_data_path(LrgModManifest *self);
void lrg_mod_manifest_set_data_path(LrgModManifest *self, const gchar *path);
```

Gets/sets relative path to mod data directory.

### lrg_mod_manifest_get_entry_point / set_entry_point

```c
const gchar *lrg_mod_manifest_get_entry_point(LrgModManifest *self);
void lrg_mod_manifest_set_entry_point(LrgModManifest *self, const gchar *entry_point);
```

Gets/sets entry point for script/native mods.

## Serialization

### lrg_mod_manifest_save_to_file

```c
gboolean lrg_mod_manifest_save_to_file(LrgModManifest *self, const gchar *path, GError **error);
```

Saves manifest to a YAML file.

**Returns:** `TRUE` on success

## YAML Format Example

```yaml
id: epic_content_pack
name: Epic Content Pack
version: 1.2.0
author: Creative Dev
description: Adds epic gear and monsters
type: content
priority: normal
data_path: data/
dependencies:
  - id: base_game
    min_version: 1.0.0
    optional: false
  - id: ui_mod
    optional: true
load_after:
  - base_textures
load_before:
  - late_stage_addon
```

## Example

```c
/* Create manifest programmatically */
g_autoptr(LrgModManifest) manifest = lrg_mod_manifest_new("my_mod");
lrg_mod_manifest_set_name(manifest, "My Awesome Mod");
lrg_mod_manifest_set_version(manifest, "1.0.0");
lrg_mod_manifest_set_author(manifest, "Your Name");
lrg_mod_manifest_set_description(manifest, "Does amazing things");
lrg_mod_manifest_set_mod_type(manifest, LRG_MOD_TYPE_CONTENT);
lrg_mod_manifest_set_priority(manifest, LRG_MOD_PRIORITY_NORMAL);
lrg_mod_manifest_set_data_path(manifest, "data/");

/* Add dependency on base game */
lrg_mod_manifest_add_dependency(manifest, "base_game", "1.0.0", FALSE);

/* Save it */
g_autoptr(GError) error = NULL;
if (!lrg_mod_manifest_save_to_file(manifest, "mod.yaml", &error))
    g_print("Save failed: %s\n", error->message);
```

## See Also

- [LrgMod](mod.md) - Uses manifest
- [LrgModManager](mod-manager.md) - Processes dependencies
