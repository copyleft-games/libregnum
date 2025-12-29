/* lrg-asset-manager.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Centralized asset loading and caching.
 *
 * The asset manager provides a unified interface for loading game
 * assets (textures, fonts, sounds, music) with caching and mod
 * overlay support through prioritized search paths.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include <gio/gio.h>
#ifdef LRG_HAS_LIBDEX
#include <libdex.h>
#endif
#include <graylib.h>
#include "../lrg-version.h"
#include "../lrg-types.h"
#include "../lrg-enums.h"

G_BEGIN_DECLS

#define LRG_TYPE_ASSET_MANAGER (lrg_asset_manager_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_DERIVABLE_TYPE (LrgAssetManager, lrg_asset_manager, LRG, ASSET_MANAGER, GObject)

/**
 * LrgAssetManagerClass:
 * @parent_class: The parent class
 * @load_texture: Virtual method to load a texture
 * @load_font: Virtual method to load a font
 * @load_sound: Virtual method to load a sound
 * @load_music: Virtual method to load music
 *
 * The class structure for #LrgAssetManager.
 *
 * Subclasses can override the virtual methods to customize
 * asset loading behavior.
 */
struct _LrgAssetManagerClass
{
    GObjectClass parent_class;

    /* Virtual methods for custom loading behavior */
    GrlTexture * (*load_texture) (LrgAssetManager  *self,
                                  const gchar      *name,
                                  GError          **error);
    GrlFont *    (*load_font)    (LrgAssetManager  *self,
                                  const gchar      *name,
                                  gint              size,
                                  GError          **error);
    GrlSound *   (*load_sound)   (LrgAssetManager  *self,
                                  const gchar      *name,
                                  GError          **error);
    GrlMusic *   (*load_music)   (LrgAssetManager  *self,
                                  const gchar      *name,
                                  GError          **error);

    /*< private >*/
    gpointer _reserved[8];
};

/* ==========================================================================
 * Construction
 * ========================================================================== */

/**
 * lrg_asset_manager_new:
 *
 * Creates a new asset manager.
 *
 * Returns: (transfer full): A new #LrgAssetManager
 */
LRG_AVAILABLE_IN_ALL
LrgAssetManager * lrg_asset_manager_new (void);

/* ==========================================================================
 * Search Path Management
 * ========================================================================== */

/**
 * lrg_asset_manager_add_search_path:
 * @self: an #LrgAssetManager
 * @path: directory path to add
 *
 * Adds a directory to the search path.
 *
 * Later paths have higher priority and will override assets from
 * earlier paths, enabling mod overlay support. Add the base game
 * assets path first, then mod paths.
 */
LRG_AVAILABLE_IN_ALL
void lrg_asset_manager_add_search_path (LrgAssetManager *self,
                                        const gchar     *path);

/**
 * lrg_asset_manager_remove_search_path:
 * @self: an #LrgAssetManager
 * @path: directory path to remove
 *
 * Removes a directory from the search path.
 *
 * Returns: %TRUE if the path was found and removed
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_asset_manager_remove_search_path (LrgAssetManager *self,
                                               const gchar     *path);

/**
 * lrg_asset_manager_clear_search_paths:
 * @self: an #LrgAssetManager
 *
 * Removes all search paths.
 */
LRG_AVAILABLE_IN_ALL
void lrg_asset_manager_clear_search_paths (LrgAssetManager *self);

/**
 * lrg_asset_manager_get_search_paths:
 * @self: an #LrgAssetManager
 *
 * Gets the list of search paths in priority order (lowest to highest).
 *
 * Returns: (transfer none) (element-type utf8): The search paths array
 */
LRG_AVAILABLE_IN_ALL
const GPtrArray * lrg_asset_manager_get_search_paths (LrgAssetManager *self);

/* ==========================================================================
 * Synchronous Loading
 * ========================================================================== */

/**
 * lrg_asset_manager_load_texture:
 * @self: an #LrgAssetManager
 * @name: relative path to texture file (e.g., "sprites/player.png")
 * @error: (nullable): return location for error
 *
 * Loads a texture from the search paths.
 *
 * If the texture is already cached, returns the cached instance.
 * Search paths are checked in reverse order (last added has priority).
 *
 * Returns: (transfer none) (nullable): The #GrlTexture, or %NULL on error
 */
LRG_AVAILABLE_IN_ALL
GrlTexture * lrg_asset_manager_load_texture (LrgAssetManager  *self,
                                             const gchar      *name,
                                             GError          **error);

/**
 * lrg_asset_manager_load_font:
 * @self: an #LrgAssetManager
 * @name: relative path to font file (e.g., "fonts/main.ttf")
 * @size: font size in pixels
 * @error: (nullable): return location for error
 *
 * Loads a font from the search paths.
 *
 * The cache key includes the size, so the same font file at different
 * sizes creates separate cache entries.
 *
 * Returns: (transfer none) (nullable): The #GrlFont, or %NULL on error
 */
LRG_AVAILABLE_IN_ALL
GrlFont * lrg_asset_manager_load_font (LrgAssetManager  *self,
                                       const gchar      *name,
                                       gint              size,
                                       GError          **error);

/**
 * lrg_asset_manager_load_sound:
 * @self: an #LrgAssetManager
 * @name: relative path to sound file (e.g., "sounds/jump.wav")
 * @error: (nullable): return location for error
 *
 * Loads a sound effect from the search paths.
 *
 * Sound files are fully loaded into memory for low-latency playback.
 *
 * Returns: (transfer none) (nullable): The #GrlSound, or %NULL on error
 */
LRG_AVAILABLE_IN_ALL
GrlSound * lrg_asset_manager_load_sound (LrgAssetManager  *self,
                                         const gchar      *name,
                                         GError          **error);

/**
 * lrg_asset_manager_load_music:
 * @self: an #LrgAssetManager
 * @name: relative path to music file (e.g., "music/theme.ogg")
 * @error: (nullable): return location for error
 *
 * Loads a streaming music track from the search paths.
 *
 * Unlike sounds, music is streamed from disk during playback.
 *
 * Returns: (transfer none) (nullable): The #GrlMusic, or %NULL on error
 */
LRG_AVAILABLE_IN_ALL
GrlMusic * lrg_asset_manager_load_music (LrgAssetManager  *self,
                                         const gchar      *name,
                                         GError          **error);

#ifdef LRG_HAS_LIBDEX
/* ==========================================================================
 * Asynchronous Loading
 * ========================================================================== */

/**
 * lrg_asset_manager_load_texture_async:
 * @self: an #LrgAssetManager
 * @name: relative path to texture file
 *
 * Asynchronously loads a texture.
 *
 * Returns: (transfer full): A #DexFuture that resolves to a #GrlTexture
 */
LRG_AVAILABLE_IN_ALL
DexFuture * lrg_asset_manager_load_texture_async (LrgAssetManager *self,
                                                  const gchar     *name);

/**
 * lrg_asset_manager_load_font_async:
 * @self: an #LrgAssetManager
 * @name: relative path to font file
 * @size: font size in pixels
 *
 * Asynchronously loads a font.
 *
 * Returns: (transfer full): A #DexFuture that resolves to a #GrlFont
 */
LRG_AVAILABLE_IN_ALL
DexFuture * lrg_asset_manager_load_font_async (LrgAssetManager *self,
                                               const gchar     *name,
                                               gint             size);

/**
 * lrg_asset_manager_load_sound_async:
 * @self: an #LrgAssetManager
 * @name: relative path to sound file
 *
 * Asynchronously loads a sound effect.
 *
 * Returns: (transfer full): A #DexFuture that resolves to a #GrlSound
 */
LRG_AVAILABLE_IN_ALL
DexFuture * lrg_asset_manager_load_sound_async (LrgAssetManager *self,
                                                const gchar     *name);

/**
 * lrg_asset_manager_load_music_async:
 * @self: an #LrgAssetManager
 * @name: relative path to music file
 *
 * Asynchronously loads a music track.
 *
 * Returns: (transfer full): A #DexFuture that resolves to a #GrlMusic
 */
LRG_AVAILABLE_IN_ALL
DexFuture * lrg_asset_manager_load_music_async (LrgAssetManager *self,
                                                const gchar     *name);
#endif /* LRG_HAS_LIBDEX */

/* ==========================================================================
 * Cache Management
 * ========================================================================== */

/**
 * lrg_asset_manager_unload:
 * @self: an #LrgAssetManager
 * @name: the asset name to unload
 *
 * Removes an asset from all caches.
 *
 * The asset may still be in use if other code holds a reference
 * to it. This removes the cache's reference, allowing the asset
 * to be freed when all other references are released.
 *
 * Returns: %TRUE if the asset was in a cache and removed
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_asset_manager_unload (LrgAssetManager *self,
                                   const gchar     *name);

/**
 * lrg_asset_manager_unload_all:
 * @self: an #LrgAssetManager
 *
 * Clears all cached assets.
 *
 * Assets currently in use elsewhere remain valid until their
 * references are released.
 */
LRG_AVAILABLE_IN_ALL
void lrg_asset_manager_unload_all (LrgAssetManager *self);

/**
 * lrg_asset_manager_is_cached:
 * @self: an #LrgAssetManager
 * @name: the asset name to check
 *
 * Checks if an asset is currently in any cache.
 *
 * Returns: %TRUE if the asset is cached
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_asset_manager_is_cached (LrgAssetManager *self,
                                      const gchar     *name);

/**
 * lrg_asset_manager_get_texture_cache_size:
 * @self: an #LrgAssetManager
 *
 * Gets the number of cached textures.
 *
 * Returns: The texture cache size
 */
LRG_AVAILABLE_IN_ALL
guint lrg_asset_manager_get_texture_cache_size (LrgAssetManager *self);

/**
 * lrg_asset_manager_get_font_cache_size:
 * @self: an #LrgAssetManager
 *
 * Gets the number of cached fonts.
 *
 * Returns: The font cache size
 */
LRG_AVAILABLE_IN_ALL
guint lrg_asset_manager_get_font_cache_size (LrgAssetManager *self);

/**
 * lrg_asset_manager_get_sound_cache_size:
 * @self: an #LrgAssetManager
 *
 * Gets the number of cached sounds.
 *
 * Returns: The sound cache size
 */
LRG_AVAILABLE_IN_ALL
guint lrg_asset_manager_get_sound_cache_size (LrgAssetManager *self);

/**
 * lrg_asset_manager_get_music_cache_size:
 * @self: an #LrgAssetManager
 *
 * Gets the number of cached music tracks.
 *
 * Returns: The music cache size
 */
LRG_AVAILABLE_IN_ALL
guint lrg_asset_manager_get_music_cache_size (LrgAssetManager *self);

G_END_DECLS
