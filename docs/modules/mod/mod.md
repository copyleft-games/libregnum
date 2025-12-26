# LrgMod

## Overview

`LrgMod` represents a single mod instance with its manifest and loading state. It provides access to mod resources and manages the mod lifecycle.

## Type Information

- **Type Name**: `LrgMod`
- **Type ID**: `LRG_TYPE_MOD`
- **Base Class**: `GObject`
- **Final Type**: Yes

## Construction

### lrg_mod_new

```c
LrgMod *lrg_mod_new(LrgModManifest *manifest, const gchar *base_path);
```

Creates a new mod from a manifest.

**Parameters:**
- `manifest` - (transfer none) Mod manifest
- `base_path` - Directory containing the mod

**Returns:** (transfer full) A new mod

**Example:**
```c
g_autoptr(LrgModManifest) manifest = lrg_mod_manifest_new("my_mod");
lrg_mod_manifest_set_name(manifest, "My Mod");
lrg_mod_manifest_set_version(manifest, "1.0.0");

g_autoptr(LrgMod) mod = lrg_mod_new(manifest, "/path/to/mod");
```

## Properties

### lrg_mod_get_manifest

```c
LrgModManifest *lrg_mod_get_manifest(LrgMod *self);
```

Gets the mod's manifest.

**Returns:** (transfer none) The manifest

### lrg_mod_get_id

```c
const gchar *lrg_mod_get_id(LrgMod *self);
```

Gets the mod's unique identifier.

### lrg_mod_get_base_path

```c
const gchar *lrg_mod_get_base_path(LrgMod *self);
```

Gets the mod's base directory path.

### lrg_mod_get_data_path

```c
const gchar *lrg_mod_get_data_path(LrgMod *self);
```

Gets the full path to the mod's data directory.

**Returns:** (transfer none) (nullable) Data path

## State

### lrg_mod_get_state

```c
LrgModState lrg_mod_get_state(LrgMod *self);
```

Gets the mod's current state.

**States:**
- `LRG_MOD_STATE_DISCOVERED` - Found but not loaded
- `LRG_MOD_STATE_LOADING` - Currently loading
- `LRG_MOD_STATE_LOADED` - Successfully loaded
- `LRG_MOD_STATE_UNLOADING` - Currently unloading
- `LRG_MOD_STATE_UNLOADED` - Not loaded
- `LRG_MOD_STATE_ERROR` - Failed to load

### lrg_mod_is_loaded

```c
gboolean lrg_mod_is_loaded(LrgMod *self);
```

Checks if the mod is fully loaded.

### lrg_mod_is_enabled / lrg_mod_set_enabled

```c
gboolean lrg_mod_is_enabled(LrgMod *self);
void lrg_mod_set_enabled(LrgMod *self, gboolean enabled);
```

Gets/sets whether the mod is enabled for loading.

### lrg_mod_get_error

```c
const gchar *lrg_mod_get_error(LrgMod *self);
```

Gets the error message if load failed.

**Returns:** (transfer none) (nullable) Error message

## Loading

### lrg_mod_load

```c
gboolean lrg_mod_load(LrgMod *self, GError **error);
```

Loads the mod.

**Returns:** `TRUE` on success

### lrg_mod_unload

```c
void lrg_mod_unload(LrgMod *self);
```

Unloads the mod.

## Resources

### lrg_mod_resolve_path

```c
gchar *lrg_mod_resolve_path(LrgMod *self, const gchar *relative_path);
```

Resolves a path relative to the mod's data directory.

**Returns:** (transfer full) (nullable) Absolute path

**Example:**
```c
gchar *config_path = lrg_mod_resolve_path(mod, "config.yaml");
if (config_path) {
    load_config(config_path);
    g_free(config_path);
}
```

### lrg_mod_list_files

```c
GPtrArray *lrg_mod_list_files(LrgMod *self, const gchar *subdir, const gchar *pattern);
```

Lists files in the mod's data directory.

**Parameters:**
- `subdir` - (nullable) Subdirectory, or NULL for root
- `pattern` - (nullable) Glob pattern, or NULL for all files

**Returns:** (transfer full) (element-type utf8) File paths

**Example:**
```c
g_autoptr(GPtrArray) yaml_files = lrg_mod_list_files(mod, "data", "*.yaml");
for (guint i = 0; i < yaml_files->len; i++) {
    const gchar *file = g_ptr_array_index(yaml_files, i);
    load_yaml_file(file);
}
```

## Example

```c
/* Create and load a mod */
g_autoptr(LrgModManifest) manifest = lrg_mod_manifest_new_from_file("mods/my_mod/mod.yaml", NULL);
g_autoptr(LrgMod) mod = lrg_mod_new(manifest, "mods/my_mod");

/* Check state before loading */
if (!lrg_mod_is_enabled(mod)) {
    g_print("Mod is disabled\n");
    lrg_mod_set_enabled(mod, TRUE);
}

/* Load the mod */
g_autoptr(GError) error = NULL;
if (lrg_mod_load(mod, &error)) {
    g_print("Mod loaded: %s\n", lrg_mod_get_id(mod));

    /* List mod files */
    g_autoptr(GPtrArray) files = lrg_mod_list_files(mod, NULL, NULL);
    g_print("Mod contains %u files\n", files->len);
} else {
    g_print("Load failed: %s\n", error->message);
}
```

## See Also

- [LrgModManifest](mod-manifest.md) - Mod metadata
- [LrgModManager](mod-manager.md) - Mod orchestration
