---
title: LrgAssetPack
module: core
type: Class
parent: GObject
since: 1.0
---

# LrgAssetPack

Resource pack management for loading assets from rres files.

> **[Home](../../index.md)** > **[Modules](../index.md)** > **[Core](index.md)** > LrgAssetPack

## Overview

LrgAssetPack wraps graylib's GrlResourcePack, providing a GObject wrapper with game-specific features like typed asset loading and integration with the data loading system.

The rres format is a raylib resource format that supports:
- Multiple asset types (textures, sounds, music, raw data)
- Optional central directory for name-based lookups
- Compression and encryption support

```c
/* Load a resource pack */
g_autoptr(GError) error = NULL;
g_autoptr(LrgAssetPack) pack = lrg_asset_pack_new ("assets.rres", &error);
if (pack == NULL) {
    g_warning ("Failed: %s", error->message);
    return;
}

/* Check contents */
if (lrg_asset_pack_has_directory (pack)) {
    g_print ("Pack has %u resources\n", lrg_asset_pack_get_resource_count (pack));
}

/* Load assets */
g_autoptr(GrlTexture) tex = lrg_asset_pack_load_texture (pack, "player.png", &error);
g_autoptr(GrlSound) sound = lrg_asset_pack_load_sound (pack, "jump.wav", &error);
```

## Construction

### From File

```c
g_autoptr(GError) error = NULL;
g_autoptr(LrgAssetPack) pack = lrg_asset_pack_new ("game_assets.rres", &error);
if (pack == NULL) {
    g_warning ("Failed to open pack: %s", error->message);
}
```

### Encrypted Pack

```c
g_autoptr(GError) error = NULL;
g_autoptr(LrgAssetPack) pack = lrg_asset_pack_new_encrypted (
    "protected_assets.rres",
    "my_secret_password",
    &error
);
```

## Key Methods

### Properties

| Method | Returns | Description |
|--------|---------|-------------|
| `lrg_asset_pack_get_filename()` | `const gchar *` | Pack file path |
| `lrg_asset_pack_get_resource_count()` | `guint` | Number of resource chunks |
| `lrg_asset_pack_get_version()` | `guint` | rres format version |
| `lrg_asset_pack_has_directory()` | `gboolean` | Has central directory |

### Directory Access

Requires the pack to have a central directory:

```c
/* List all resources */
g_autoptr(GList) names = lrg_asset_pack_list_resources (pack);
for (GList *l = names; l != NULL; l = l->next) {
    g_print ("Resource: %s\n", (gchar *)l->data);
}
g_list_free_full (names, g_free);

/* Check existence */
if (lrg_asset_pack_contains (pack, "sprites/player.png")) {
    /* Resource exists */
}

/* Get ID for name */
guint32 id = lrg_asset_pack_get_id (pack, "sprites/player.png");

/* Get name for ID */
g_autofree gchar *name = lrg_asset_pack_get_name (pack, id);
```

### Typed Asset Loading

```c
g_autoptr(GError) error = NULL;

/* Load texture */
GrlTexture *texture = lrg_asset_pack_load_texture (pack, "player.png", &error);

/* Load sound */
GrlSound *sound = lrg_asset_pack_load_sound (pack, "jump.wav", &error);

/* Load wave data (for manipulation) */
LrgWaveData *wave = lrg_asset_pack_load_wave (pack, "explosion.wav", &error);

/* Load music (streamed from pack) */
GrlMusic *music = lrg_asset_pack_load_music (pack, "background.ogg", &error);
```

### Raw Data Loading

```c
gsize size;
g_autofree guint8 *data = lrg_asset_pack_load_raw (pack, "config.yaml", &size, &error);
if (data) {
    /* Process raw data */
}

/* Or by ID */
guint32 id = lrg_asset_pack_get_id (pack, "config.yaml");
g_autofree guint8 *data2 = lrg_asset_pack_load_raw_by_id (pack, id, &size, &error);
```

### GObject Loading

Load GObjects from YAML resources in the pack:

```c
LrgDataLoader *loader = lrg_engine_get_data_loader (engine);

g_autoptr(GObject) obj = lrg_asset_pack_load_object (
    pack, "entities/player.yaml", loader, &error
);
if (obj) {
    g_print ("Loaded: %s\n", G_OBJECT_TYPE_NAME (obj));
}
```

### Access Underlying

```c
/* Get the wrapped GrlResourcePack */
GrlResourcePack *grl_pack = lrg_asset_pack_get_resource_pack (pack);
```

## Error Handling

```c
g_autoptr(GError) error = NULL;
g_autoptr(LrgAssetPack) pack = lrg_asset_pack_new ("missing.rres", &error);

if (pack == NULL) {
    if (error->domain == LRG_ASSET_PACK_ERROR) {
        switch (error->code) {
        case LRG_ASSET_PACK_ERROR_FILE_NOT_FOUND:
            g_print ("File not found\n");
            break;
        case LRG_ASSET_PACK_ERROR_INVALID_FORMAT:
            g_print ("Invalid rres format\n");
            break;
        case LRG_ASSET_PACK_ERROR_DECRYPT_FAILED:
            g_print ("Wrong password\n");
            break;
        }
    }
}
```

Error codes:
- `LRG_ASSET_PACK_ERROR_FILE_NOT_FOUND` - File could not be opened
- `LRG_ASSET_PACK_ERROR_INVALID_FORMAT` - Invalid rres file format
- `LRG_ASSET_PACK_ERROR_RESOURCE_NOT_FOUND` - Resource not found in pack
- `LRG_ASSET_PACK_ERROR_LOAD_FAILED` - Failed to load resource
- `LRG_ASSET_PACK_ERROR_DECRYPT_FAILED` - Failed to decrypt resource

## Complete Example

```c
#include <libregnum.h>

static void
load_game_assets (LrgEngine *engine)
{
    g_autoptr(GError) error = NULL;

    /* Open the main asset pack */
    g_autoptr(LrgAssetPack) pack = lrg_asset_pack_new ("game.rres", &error);
    if (pack == NULL) {
        g_warning ("Failed to open asset pack: %s", error->message);
        return;
    }

    g_print ("Opened pack: %s\n", lrg_asset_pack_get_filename (pack));
    g_print ("Resources: %u\n", lrg_asset_pack_get_resource_count (pack));

    /* Check for central directory */
    if (!lrg_asset_pack_has_directory (pack)) {
        g_warning ("Pack has no directory, cannot use name-based loading");
        return;
    }

    /* List all resources */
    g_autoptr(GList) resources = lrg_asset_pack_list_resources (pack);
    for (GList *l = resources; l != NULL; l = l->next) {
        g_print ("  - %s\n", (gchar *)l->data);
    }

    /* Load specific assets */
    g_autoptr(GrlTexture) player_tex = lrg_asset_pack_load_texture (
        pack, "sprites/player.png", &error
    );
    if (player_tex == NULL) {
        g_warning ("Failed to load player texture: %s", error->message);
    }

    /* Load wave data for procedural manipulation */
    g_autoptr(LrgWaveData) explosion = lrg_asset_pack_load_wave (
        pack, "sounds/explosion.wav", &error
    );
    if (explosion) {
        /* Modify and use */
        g_autoptr(LrgWaveData) pitched = lrg_wave_data_resample (explosion, 22050);
        /* ... */
    }

    /* Load game objects from YAML in pack */
    LrgDataLoader *loader = lrg_engine_get_data_loader (engine);
    g_autoptr(GObject) player = lrg_asset_pack_load_object (
        pack, "entities/player.yaml", loader, &error
    );
}

int main (void)
{
    g_autoptr(GError) error = NULL;
    LrgEngine *engine = lrg_engine_get_default ();

    if (!lrg_engine_startup (engine, &error)) {
        g_error ("Startup failed: %s", error->message);
    }

    load_game_assets (engine);

    lrg_engine_shutdown (engine);
    return 0;
}
```

## Using with LrgAssetManager

LrgAssetPack can be registered with LrgAssetManager for unified asset loading:

```c
LrgAssetManager *manager = lrg_engine_get_asset_manager (engine);

/* Load pack and register */
g_autoptr(LrgAssetPack) pack = lrg_asset_pack_new ("dlc.rres", &error);
lrg_asset_manager_add_pack (manager, "dlc", pack);

/* Now assets can be loaded through the manager */
GrlTexture *tex = lrg_asset_manager_load_texture (manager, "dlc:player.png", &error);
```

## See Also

- [LrgAssetManager](asset-manager.md) - Asset caching and management
- [LrgDataLoader](data-loader.md) - YAML object loading
- [LrgWaveData](../audio/wave-data.md) - Wave data manipulation
