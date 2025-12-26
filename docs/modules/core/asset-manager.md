---
title: LrgAssetManager
module: core
type: Class
parent: GObject
since: 1.0
---

# LrgAssetManager

Centralized asset loading and caching with mod overlay support.

> **[Home](../../index.md)** > **[Modules](../index.md)** > **[Core](index.md)** > LrgAssetManager

## Overview

LrgAssetManager provides unified asset loading with caching and mod overlay:

```c
/* Add search paths */
lrg_asset_manager_add_search_path (manager, "base/assets/");
lrg_asset_manager_add_search_path (manager, "mods/my-mod/");

/* Load asset - searches in reverse priority */
GrlTexture *tex = lrg_asset_manager_load_texture (manager, "sprites/player.png", &error);
```

Asset types: Textures, Fonts, Sounds, Music

## Key Methods

### Search Paths

- `lrg_asset_manager_add_search_path()` - Add directory
- `lrg_asset_manager_remove_search_path()` - Remove directory
- `lrg_asset_manager_clear_search_paths()` - Clear all paths
- `lrg_asset_manager_get_search_paths()` - List paths

### Asset Loading

- `lrg_asset_manager_load_texture()` - Load image
- `lrg_asset_manager_load_font()` - Load font file
- `lrg_asset_manager_load_sound()` - Load sound effect
- `lrg_asset_manager_load_music()` - Load music track

### Async Loading

- `lrg_asset_manager_load_texture_async()`
- `lrg_asset_manager_load_font_async()`
- `lrg_asset_manager_load_sound_async()`
- `lrg_asset_manager_load_music_async()`

### Cache Management

- `lrg_asset_manager_unload()` - Remove from cache
- `lrg_asset_manager_unload_all()` - Clear cache
- `lrg_asset_manager_is_cached()` - Check if cached
- `lrg_asset_manager_get_texture_cache_size()` - Count textures
- `lrg_asset_manager_get_font_cache_size()` - Count fonts
- `lrg_asset_manager_get_sound_cache_size()` - Count sounds
- `lrg_asset_manager_get_music_cache_size()` - Count music

## Complete API Reference

See `/var/home/zach/Source/Projects/libregnum/src/core/lrg-asset-manager.h` for full function signatures.

## Example

```c
#include <libregnum.h>

int main ()
{
    g_autoptr(GError) error = NULL;
    LrgAssetManager *manager = lrg_asset_manager_new ();

    /* Setup search paths */
    lrg_asset_manager_add_search_path (manager, "./assets/base/");
    lrg_asset_manager_add_search_path (manager, "./assets/mods/mod1/");

    /* Load assets */
    GrlTexture *player = lrg_asset_manager_load_texture (manager, "sprites/player.png", &error);
    GrlFont *font = lrg_asset_manager_load_font (manager, "fonts/main.ttf", 16, &error);

    if (player)
        g_print ("Loaded player texture\n");

    g_object_unref (manager);
    return 0;
}
```

## See Also

- [Asset Management](../../architecture.md#asset-manager-pattern)
- [Modding System](../../concepts/modding.md)
- [LrgEngine](engine.md)
