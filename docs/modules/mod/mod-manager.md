# LrgModManager

## Overview

`LrgModManager` is the central orchestrator for mod lifecycle management. It handles discovery, dependency resolution, load ordering, and provides access to mod content through provider queries.

## Type Information

- **Type Name**: `LrgModManager`
- **Type ID**: `LRG_TYPE_MOD_MANAGER`
- **Base Class**: `GObject`
- **Final Type**: Yes
- **Singleton**: Yes (default instance)

## Construction

### lrg_mod_manager_get_default

```c
LrgModManager *lrg_mod_manager_get_default(void);
```

Gets the default (singleton) mod manager instance.

**Returns:** (transfer none) The default manager

### lrg_mod_manager_new

```c
LrgModManager *lrg_mod_manager_new(void);
```

Creates a new mod manager instance.

**Returns:** (transfer full) A new manager

## Configuration

### lrg_mod_manager_add_search_path

```c
void lrg_mod_manager_add_search_path(LrgModManager *self, const gchar *path);
```

Adds a directory to search for mods.

**Example:**
```c
g_autoptr(LrgModManager) mgr = lrg_mod_manager_get_default();
lrg_mod_manager_add_search_path(mgr, "/opt/libregnum/mods");
lrg_mod_manager_add_search_path(mgr, g_build_filename(g_get_user_data_dir(), "libregnum/mods", NULL));
```

### lrg_mod_manager_get_loader

```c
LrgModLoader *lrg_mod_manager_get_loader(LrgModManager *self);
```

Gets the underlying mod loader for advanced configuration.

**Returns:** (transfer none) The mod loader

## Discovery and Loading

### lrg_mod_manager_discover

```c
guint lrg_mod_manager_discover(LrgModManager *self, GError **error);
```

Discovers mods in all search paths.

**Returns:** Number of mods discovered

### lrg_mod_manager_load_all

```c
gboolean lrg_mod_manager_load_all(LrgModManager *self, GError **error);
```

Loads all discovered enabled mods in dependency order.

**Returns:** `TRUE` if all required mods loaded successfully

**Example:**
```c
g_autoptr(GError) error = NULL;
if (!lrg_mod_manager_load_all(mgr, &error)) {
    g_warning("Mod loading failed: %s", error->message);
    return FALSE;
}
```

### lrg_mod_manager_unload_all

```c
void lrg_mod_manager_unload_all(LrgModManager *self);
```

Unloads all mods in reverse load order.

### lrg_mod_manager_reload

```c
gboolean lrg_mod_manager_reload(LrgModManager *self, GError **error);
```

Reloads all mods (unload then load).

## Mod Queries

### lrg_mod_manager_get_mods

```c
GPtrArray *lrg_mod_manager_get_mods(LrgModManager *self);
```

Gets all discovered mods.

**Returns:** (transfer none) (element-type LrgMod) Array of mods

### lrg_mod_manager_get_loaded_mods

```c
GPtrArray *lrg_mod_manager_get_loaded_mods(LrgModManager *self);
```

Gets all loaded mods in load order.

**Returns:** (transfer none) (element-type LrgMod) Loaded mods in order

### lrg_mod_manager_get_mod

```c
LrgMod *lrg_mod_manager_get_mod(LrgModManager *self, const gchar *mod_id);
```

Gets a mod by ID.

**Returns:** (transfer none) (nullable) The mod, or NULL

### lrg_mod_manager_has_mod

```c
gboolean lrg_mod_manager_has_mod(LrgModManager *self, const gchar *mod_id);
```

Checks if a mod is discovered.

### lrg_mod_manager_is_mod_loaded

```c
gboolean lrg_mod_manager_is_mod_loaded(LrgModManager *self, const gchar *mod_id);
```

Checks if a mod is loaded.

## Mod Control

### lrg_mod_manager_enable_mod / lrg_mod_manager_disable_mod

```c
gboolean lrg_mod_manager_enable_mod(LrgModManager *self, const gchar *mod_id);
gboolean lrg_mod_manager_disable_mod(LrgModManager *self, const gchar *mod_id);
```

Enables/disables a mod. Must call before loading.

**Returns:** `TRUE` if mod found

## Load Order

### lrg_mod_manager_get_load_order

```c
GPtrArray *lrg_mod_manager_get_load_order(LrgModManager *self);
```

Gets the computed load order.

**Returns:** (transfer full) (element-type utf8) Mod IDs in order

### lrg_mod_manager_check_dependencies

```c
gboolean lrg_mod_manager_check_dependencies(LrgModManager *self, LrgMod *mod, GError **error);
```

Checks if all dependencies for a mod are satisfied.

**Returns:** `TRUE` if dependencies met

## Resource Resolution

### lrg_mod_manager_resolve_path

```c
gchar *lrg_mod_manager_resolve_path(LrgModManager *self, const gchar *path);
```

Resolves a path, checking loaded mods in reverse order (most recent wins).

Allows mods to override base game resources.

**Returns:** (transfer full) (nullable) Absolute path to resource

**Example:**
```c
gchar *resolved = lrg_mod_manager_resolve_path(mgr, "models/player.obj");
if (resolved) {
    load_model(resolved);
    g_free(resolved);
}
```

## Provider Collection

### lrg_mod_manager_collect_entity_types

```c
GList *lrg_mod_manager_collect_entity_types(LrgModManager *self);
```

Collects entity types from all mods implementing `LrgEntityProvider`.

**Returns:** (transfer container) (element-type GType) List of GTypes

### lrg_mod_manager_collect_item_defs

```c
GList *lrg_mod_manager_collect_item_defs(LrgModManager *self);
```

Collects item definitions from `LrgItemProvider` mods.

**Returns:** (transfer container) (element-type LrgItemDef) Item list

### lrg_mod_manager_collect_dialog_trees

```c
GList *lrg_mod_manager_collect_dialog_trees(LrgModManager *self);
```

Collects dialog trees from `LrgDialogProvider` mods.

### lrg_mod_manager_collect_quest_defs

```c
GList *lrg_mod_manager_collect_quest_defs(LrgModManager *self);
```

Collects quest definitions from `LrgQuestProvider` mods.

### lrg_mod_manager_collect_bt_node_types

```c
GList *lrg_mod_manager_collect_bt_node_types(LrgModManager *self);
```

Collects behavior tree node types from `LrgAIProvider` mods.

### lrg_mod_manager_collect_commands

```c
GList *lrg_mod_manager_collect_commands(LrgModManager *self);
```

Collects console commands from `LrgCommandProvider` mods.

### lrg_mod_manager_collect_locales

```c
GList *lrg_mod_manager_collect_locales(LrgModManager *self);
```

Collects locales from `LrgLocaleProvider` mods.

### lrg_mod_manager_collect_scenes

```c
GList *lrg_mod_manager_collect_scenes(LrgModManager *self);
```

Collects scenes from `LrgSceneProvider` mods.

## Example

```c
/* Initialize mod system */
g_autoptr(LrgModManager) mgr = lrg_mod_manager_get_default();
lrg_mod_manager_add_search_path(mgr, "mods/");

/* Discover */
guint count = lrg_mod_manager_discover(mgr, NULL);
g_print("Found %u mods\n", count);

/* Show discovered mods */
g_autoptr(GPtrArray) mods = lrg_mod_manager_get_mods(mgr);
for (guint i = 0; i < mods->len; i++) {
    LrgMod *mod = g_ptr_array_index(mods, i);
    g_print("  - %s (%s)\n", lrg_mod_get_id(mod), lrg_mod_get_state(mod) ? "loaded" : "not loaded");
}

/* Load */
g_autoptr(GError) error = NULL;
if (lrg_mod_manager_load_all(mgr, &error)) {
    g_print("All mods loaded\n");
} else {
    g_print("Load error: %s\n", error->message);
}

/* Register content from providers */
g_autoptr(GList) entities = lrg_mod_manager_collect_entity_types(mgr);
for (GList *l = entities; l; l = l->next) {
    GType entity_type = GPOINTER_TO_INT(l->data);
    register_entity_type(entity_type);
}
```

## See Also

- [LrgModLoader](mod-loader.md) - Discovery
- [LrgMod](mod.md) - Individual mods
- [LrgModable](modable.md) - Mod interface
