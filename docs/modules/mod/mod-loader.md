# LrgModLoader

## Overview

`LrgModLoader` handles filesystem scanning and mod manifest parsing. It discovers mods and loads individual mod instances.

## Type Information

- **Type Name**: `LrgModLoader`
- **Type ID**: `LRG_TYPE_MOD_LOADER`
- **Base Class**: `GObject`
- **Final Type**: Yes

## Construction

### lrg_mod_loader_new

```c
LrgModLoader *lrg_mod_loader_new(void);
```

Creates a new mod loader.

**Returns:** (transfer full) A new loader

## Search Paths

### lrg_mod_loader_add_search_path

```c
void lrg_mod_loader_add_search_path(LrgModLoader *self, const gchar *path);
```

Adds a directory to search for mods.

### lrg_mod_loader_get_search_paths

```c
GPtrArray *lrg_mod_loader_get_search_paths(LrgModLoader *self);
```

Gets all search paths.

**Returns:** (transfer none) (element-type utf8) Path list

### lrg_mod_loader_clear_search_paths

```c
void lrg_mod_loader_clear_search_paths(LrgModLoader *self);
```

Removes all search paths.

## Configuration

### lrg_mod_loader_get_manifest_filename / lrg_mod_loader_set_manifest_filename

```c
const gchar *lrg_mod_loader_get_manifest_filename(LrgModLoader *self);
void lrg_mod_loader_set_manifest_filename(LrgModLoader *self, const gchar *filename);
```

Gets/sets the manifest filename (default: "mod.yaml").

**Example:**
```c
lrg_mod_loader_set_manifest_filename(loader, "modinfo.yaml");
```

## Discovery

### lrg_mod_loader_discover

```c
GPtrArray *lrg_mod_loader_discover(LrgModLoader *self, GError **error);
```

Discovers mods in all search paths.

**Returns:** (transfer full) (element-type LrgMod) Discovered mods

### lrg_mod_loader_discover_at

```c
GPtrArray *lrg_mod_loader_discover_at(LrgModLoader *self, const gchar *path, GError **error);
```

Discovers mods at a specific path.

**Returns:** (transfer full) (element-type LrgMod) Mods at path

## Loading

### lrg_mod_loader_load_mod

```c
LrgMod *lrg_mod_loader_load_mod(LrgModLoader *self, const gchar *path, GError **error);
```

Loads a single mod from a directory.

**Parameters:**
- `path` - Path to mod directory containing manifest

**Returns:** (transfer full) (nullable) Loaded mod, or NULL on error

**Example:**
```c
g_autoptr(GError) error = NULL;
g_autoptr(LrgMod) mod = lrg_mod_loader_load_mod(loader, "/path/to/my_mod", &error);
if (!mod)
    g_print("Failed to load mod: %s\n", error->message);
```

## See Also

- [LrgModManager](mod-manager.md) - Uses loader
- [LrgMod](mod.md) - Loaded mod representation
