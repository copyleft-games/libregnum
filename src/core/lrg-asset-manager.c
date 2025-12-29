/* lrg-asset-manager.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Asset manager implementation with caching and mod overlay support.
 */

#include "config.h"

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_CORE

#include "lrg-asset-manager.h"
#include "../lrg-log.h"

typedef struct
{
    GPtrArray  *search_paths;    /* Array of gchar* (owned strings) */
    GHashTable *texture_cache;   /* gchar* -> GrlTexture* */
    GHashTable *font_cache;      /* gchar* (name:size) -> GrlFont* */
    GHashTable *sound_cache;     /* gchar* -> GrlSound* */
    GHashTable *music_cache;     /* gchar* -> GrlMusic* */
} LrgAssetManagerPrivate;

G_DEFINE_TYPE_WITH_PRIVATE (LrgAssetManager, lrg_asset_manager, G_TYPE_OBJECT)

/* ==========================================================================
 * Private Helpers
 * ========================================================================== */

/**
 * resolve_asset_path:
 * @self: an #LrgAssetManager
 * @name: the relative asset name
 *
 * Resolves an asset name to a full path by searching paths in reverse
 * order (last added has highest priority).
 *
 * Returns: (transfer full) (nullable): The full path, or %NULL if not found
 */
static gchar *
resolve_asset_path (LrgAssetManager *self,
                    const gchar     *name)
{
    LrgAssetManagerPrivate *priv;
    guint                   i;

    priv = lrg_asset_manager_get_instance_private (self);

    /* Search in reverse order (last path has highest priority) */
    for (i = priv->search_paths->len; i > 0; i--)
    {
        const gchar      *search_path;
        g_autofree gchar *full_path = NULL;

        search_path = g_ptr_array_index (priv->search_paths, i - 1);
        full_path = g_build_filename (search_path, name, NULL);

        if (g_file_test (full_path, G_FILE_TEST_EXISTS))
        {
            return g_steal_pointer (&full_path);
        }
    }

    return NULL;
}

/**
 * make_font_cache_key:
 * @name: the font name
 * @size: the font size
 *
 * Creates a cache key for a font that includes the size.
 *
 * Returns: (transfer full): The cache key (name:size)
 */
static gchar *
make_font_cache_key (const gchar *name,
                     gint         size)
{
    return g_strdup_printf ("%s:%d", name, size);
}

/* ==========================================================================
 * Virtual Method Implementations
 * ========================================================================== */

static GrlTexture *
lrg_asset_manager_real_load_texture (LrgAssetManager  *self,
                                     const gchar      *name,
                                     GError          **error)
{
    LrgAssetManagerPrivate *priv;
    GrlTexture             *texture;
    g_autofree gchar       *path = NULL;

    priv = lrg_asset_manager_get_instance_private (self);

    /* Check cache first */
    texture = g_hash_table_lookup (priv->texture_cache, name);
    if (texture != NULL)
    {
        return texture;
    }

    /* Resolve path */
    path = resolve_asset_path (self, name);
    if (path == NULL)
    {
        g_set_error (error,
                     LRG_ASSET_MANAGER_ERROR,
                     LRG_ASSET_MANAGER_ERROR_NOT_FOUND,
                     "Texture not found: %s",
                     name);
        return NULL;
    }

    /* Load texture */
    texture = grl_texture_new_from_file (path);
    if (texture == NULL || !grl_texture_is_valid (texture))
    {
        g_clear_object (&texture);
        g_set_error (error,
                     LRG_ASSET_MANAGER_ERROR,
                     LRG_ASSET_MANAGER_ERROR_LOAD_FAILED,
                     "Failed to load texture: %s",
                     path);
        return NULL;
    }

    /* Cache and return */
    g_hash_table_insert (priv->texture_cache, g_strdup (name), texture);

    lrg_debug (LRG_LOG_DOMAIN_CORE,
               "Loaded texture '%s' from %s",
               name, path);

    return texture;
}

static GrlFont *
lrg_asset_manager_real_load_font (LrgAssetManager  *self,
                                  const gchar      *name,
                                  gint              size,
                                  GError          **error)
{
    LrgAssetManagerPrivate *priv;
    GrlFont                *font;
    g_autofree gchar       *cache_key = NULL;
    g_autofree gchar       *path = NULL;

    priv = lrg_asset_manager_get_instance_private (self);

    /* Check cache first (key includes size) */
    cache_key = make_font_cache_key (name, size);
    font = g_hash_table_lookup (priv->font_cache, cache_key);
    if (font != NULL)
    {
        return font;
    }

    /* Resolve path */
    path = resolve_asset_path (self, name);
    if (path == NULL)
    {
        g_set_error (error,
                     LRG_ASSET_MANAGER_ERROR,
                     LRG_ASSET_MANAGER_ERROR_NOT_FOUND,
                     "Font not found: %s",
                     name);
        return NULL;
    }

    /* Load font */
    font = grl_font_new_from_file_ex (path, size, NULL, 0);
    if (font == NULL)
    {
        g_set_error (error,
                     LRG_ASSET_MANAGER_ERROR,
                     LRG_ASSET_MANAGER_ERROR_LOAD_FAILED,
                     "Failed to load font: %s",
                     path);
        return NULL;
    }

    /* Cache and return */
    g_hash_table_insert (priv->font_cache, g_steal_pointer (&cache_key), font);

    lrg_debug (LRG_LOG_DOMAIN_CORE,
               "Loaded font '%s' size %d from %s",
               name, size, path);

    return font;
}

static GrlSound *
lrg_asset_manager_real_load_sound (LrgAssetManager  *self,
                                   const gchar      *name,
                                   GError          **error)
{
    LrgAssetManagerPrivate *priv;
    GrlSound               *sound;
    g_autofree gchar       *path = NULL;

    priv = lrg_asset_manager_get_instance_private (self);

    /* Check cache first */
    sound = g_hash_table_lookup (priv->sound_cache, name);
    if (sound != NULL)
    {
        return sound;
    }

    /* Resolve path */
    path = resolve_asset_path (self, name);
    if (path == NULL)
    {
        g_set_error (error,
                     LRG_ASSET_MANAGER_ERROR,
                     LRG_ASSET_MANAGER_ERROR_NOT_FOUND,
                     "Sound not found: %s",
                     name);
        return NULL;
    }

    /* Load sound */
    sound = grl_sound_new_from_file (path, error);
    if (sound == NULL)
    {
        /* Error already set by grl_sound_new_from_file */
        return NULL;
    }

    /* Cache and return */
    g_hash_table_insert (priv->sound_cache, g_strdup (name), sound);

    lrg_debug (LRG_LOG_DOMAIN_CORE,
               "Loaded sound '%s' from %s",
               name, path);

    return sound;
}

static GrlMusic *
lrg_asset_manager_real_load_music (LrgAssetManager  *self,
                                   const gchar      *name,
                                   GError          **error)
{
    LrgAssetManagerPrivate *priv;
    GrlMusic               *music;
    g_autofree gchar       *path = NULL;

    priv = lrg_asset_manager_get_instance_private (self);

    /* Check cache first */
    music = g_hash_table_lookup (priv->music_cache, name);
    if (music != NULL)
    {
        return music;
    }

    /* Resolve path */
    path = resolve_asset_path (self, name);
    if (path == NULL)
    {
        g_set_error (error,
                     LRG_ASSET_MANAGER_ERROR,
                     LRG_ASSET_MANAGER_ERROR_NOT_FOUND,
                     "Music not found: %s",
                     name);
        return NULL;
    }

    /* Load music */
    music = grl_music_new_from_file (path, error);
    if (music == NULL)
    {
        /* Error already set by grl_music_new_from_file */
        return NULL;
    }

    /* Cache and return */
    g_hash_table_insert (priv->music_cache, g_strdup (name), music);

    lrg_debug (LRG_LOG_DOMAIN_CORE,
               "Loaded music '%s' from %s",
               name, path);

    return music;
}

/* ==========================================================================
 * GObject Implementation
 * ========================================================================== */

static void
lrg_asset_manager_finalize (GObject *object)
{
    LrgAssetManager        *self = LRG_ASSET_MANAGER (object);
    LrgAssetManagerPrivate *priv = lrg_asset_manager_get_instance_private (self);

    g_clear_pointer (&priv->search_paths, g_ptr_array_unref);
    g_clear_pointer (&priv->texture_cache, g_hash_table_unref);
    g_clear_pointer (&priv->font_cache, g_hash_table_unref);
    g_clear_pointer (&priv->sound_cache, g_hash_table_unref);
    g_clear_pointer (&priv->music_cache, g_hash_table_unref);

    G_OBJECT_CLASS (lrg_asset_manager_parent_class)->finalize (object);
}

static void
lrg_asset_manager_class_init (LrgAssetManagerClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->finalize = lrg_asset_manager_finalize;

    /* Set default virtual method implementations */
    klass->load_texture = lrg_asset_manager_real_load_texture;
    klass->load_font = lrg_asset_manager_real_load_font;
    klass->load_sound = lrg_asset_manager_real_load_sound;
    klass->load_music = lrg_asset_manager_real_load_music;
}

static void
lrg_asset_manager_init (LrgAssetManager *self)
{
    LrgAssetManagerPrivate *priv = lrg_asset_manager_get_instance_private (self);

    priv->search_paths = g_ptr_array_new_with_free_func (g_free);
    priv->texture_cache = g_hash_table_new_full (g_str_hash, g_str_equal,
                                                  g_free, g_object_unref);
    priv->font_cache = g_hash_table_new_full (g_str_hash, g_str_equal,
                                               g_free, g_object_unref);
    priv->sound_cache = g_hash_table_new_full (g_str_hash, g_str_equal,
                                                g_free, g_object_unref);
    priv->music_cache = g_hash_table_new_full (g_str_hash, g_str_equal,
                                                g_free, g_object_unref);
}

/* ==========================================================================
 * Public API - Construction
 * ========================================================================== */

/**
 * lrg_asset_manager_new:
 *
 * Creates a new asset manager.
 *
 * Returns: (transfer full): A new #LrgAssetManager
 */
LrgAssetManager *
lrg_asset_manager_new (void)
{
    return g_object_new (LRG_TYPE_ASSET_MANAGER, NULL);
}

/* ==========================================================================
 * Public API - Search Path Management
 * ========================================================================== */

/**
 * lrg_asset_manager_add_search_path:
 * @self: an #LrgAssetManager
 * @path: directory path to add
 *
 * Adds a directory to the search path.
 */
void
lrg_asset_manager_add_search_path (LrgAssetManager *self,
                                   const gchar     *path)
{
    LrgAssetManagerPrivate *priv;

    g_return_if_fail (LRG_IS_ASSET_MANAGER (self));
    g_return_if_fail (path != NULL);

    priv = lrg_asset_manager_get_instance_private (self);

    g_ptr_array_add (priv->search_paths, g_strdup (path));

    lrg_debug (LRG_LOG_DOMAIN_CORE,
               "Added asset search path: %s",
               path);
}

/**
 * lrg_asset_manager_remove_search_path:
 * @self: an #LrgAssetManager
 * @path: directory path to remove
 *
 * Removes a directory from the search path.
 *
 * Returns: %TRUE if the path was found and removed
 */
gboolean
lrg_asset_manager_remove_search_path (LrgAssetManager *self,
                                      const gchar     *path)
{
    LrgAssetManagerPrivate *priv;
    guint                   i;

    g_return_val_if_fail (LRG_IS_ASSET_MANAGER (self), FALSE);
    g_return_val_if_fail (path != NULL, FALSE);

    priv = lrg_asset_manager_get_instance_private (self);

    for (i = 0; i < priv->search_paths->len; i++)
    {
        const gchar *existing = g_ptr_array_index (priv->search_paths, i);

        if (g_strcmp0 (existing, path) == 0)
        {
            g_ptr_array_remove_index (priv->search_paths, i);
            lrg_debug (LRG_LOG_DOMAIN_CORE,
                       "Removed asset search path: %s",
                       path);
            return TRUE;
        }
    }

    return FALSE;
}

/**
 * lrg_asset_manager_clear_search_paths:
 * @self: an #LrgAssetManager
 *
 * Removes all search paths.
 */
void
lrg_asset_manager_clear_search_paths (LrgAssetManager *self)
{
    LrgAssetManagerPrivate *priv;

    g_return_if_fail (LRG_IS_ASSET_MANAGER (self));

    priv = lrg_asset_manager_get_instance_private (self);

    g_ptr_array_set_size (priv->search_paths, 0);

    lrg_debug (LRG_LOG_DOMAIN_CORE, "Cleared all asset search paths");
}

/**
 * lrg_asset_manager_get_search_paths:
 * @self: an #LrgAssetManager
 *
 * Gets the list of search paths.
 *
 * Returns: (transfer none) (element-type utf8): The search paths array
 */
const GPtrArray *
lrg_asset_manager_get_search_paths (LrgAssetManager *self)
{
    LrgAssetManagerPrivate *priv;

    g_return_val_if_fail (LRG_IS_ASSET_MANAGER (self), NULL);

    priv = lrg_asset_manager_get_instance_private (self);

    return priv->search_paths;
}

/* ==========================================================================
 * Public API - Synchronous Loading
 * ========================================================================== */

/**
 * lrg_asset_manager_load_texture:
 * @self: an #LrgAssetManager
 * @name: relative path to texture file
 * @error: (nullable): return location for error
 *
 * Loads a texture from the search paths.
 *
 * Returns: (transfer none) (nullable): The #GrlTexture, or %NULL on error
 */
GrlTexture *
lrg_asset_manager_load_texture (LrgAssetManager  *self,
                                const gchar      *name,
                                GError          **error)
{
    LrgAssetManagerClass *klass;

    g_return_val_if_fail (LRG_IS_ASSET_MANAGER (self), NULL);
    g_return_val_if_fail (name != NULL, NULL);

    klass = LRG_ASSET_MANAGER_GET_CLASS (self);

    return klass->load_texture (self, name, error);
}

/**
 * lrg_asset_manager_load_font:
 * @self: an #LrgAssetManager
 * @name: relative path to font file
 * @size: font size in pixels
 * @error: (nullable): return location for error
 *
 * Loads a font from the search paths.
 *
 * Returns: (transfer none) (nullable): The #GrlFont, or %NULL on error
 */
GrlFont *
lrg_asset_manager_load_font (LrgAssetManager  *self,
                             const gchar      *name,
                             gint              size,
                             GError          **error)
{
    LrgAssetManagerClass *klass;

    g_return_val_if_fail (LRG_IS_ASSET_MANAGER (self), NULL);
    g_return_val_if_fail (name != NULL, NULL);
    g_return_val_if_fail (size > 0, NULL);

    klass = LRG_ASSET_MANAGER_GET_CLASS (self);

    return klass->load_font (self, name, size, error);
}

/**
 * lrg_asset_manager_load_sound:
 * @self: an #LrgAssetManager
 * @name: relative path to sound file
 * @error: (nullable): return location for error
 *
 * Loads a sound effect from the search paths.
 *
 * Returns: (transfer none) (nullable): The #GrlSound, or %NULL on error
 */
GrlSound *
lrg_asset_manager_load_sound (LrgAssetManager  *self,
                              const gchar      *name,
                              GError          **error)
{
    LrgAssetManagerClass *klass;

    g_return_val_if_fail (LRG_IS_ASSET_MANAGER (self), NULL);
    g_return_val_if_fail (name != NULL, NULL);

    klass = LRG_ASSET_MANAGER_GET_CLASS (self);

    return klass->load_sound (self, name, error);
}

/**
 * lrg_asset_manager_load_music:
 * @self: an #LrgAssetManager
 * @name: relative path to music file
 * @error: (nullable): return location for error
 *
 * Loads a streaming music track from the search paths.
 *
 * Returns: (transfer none) (nullable): The #GrlMusic, or %NULL on error
 */
GrlMusic *
lrg_asset_manager_load_music (LrgAssetManager  *self,
                              const gchar      *name,
                              GError          **error)
{
    LrgAssetManagerClass *klass;

    g_return_val_if_fail (LRG_IS_ASSET_MANAGER (self), NULL);
    g_return_val_if_fail (name != NULL, NULL);

    klass = LRG_ASSET_MANAGER_GET_CLASS (self);

    return klass->load_music (self, name, error);
}

#ifdef LRG_HAS_LIBDEX
/* ==========================================================================
 * Async Loading - Data Structures
 * ========================================================================== */

typedef struct
{
    LrgAssetManager *manager;
    gchar           *name;
    gint             size;  /* For fonts only */
} LoadAssetData;

static void
load_asset_data_free (LoadAssetData *data)
{
    g_clear_object (&data->manager);
    g_free (data->name);
    g_free (data);
}

/* ==========================================================================
 * Async Loading - Fiber Functions
 * ========================================================================== */

static DexFuture *
load_texture_fiber (gpointer user_data)
{
    LoadAssetData    *data = user_data;
    g_autoptr(GError) error = NULL;
    GrlTexture       *texture;

    texture = lrg_asset_manager_load_texture (data->manager, data->name, &error);
    if (texture == NULL)
    {
        return dex_future_new_for_error (g_steal_pointer (&error));
    }

    /* Return ref'd pointer since cache owns primary reference */
    return dex_future_new_for_pointer (g_object_ref (texture));
}

static DexFuture *
load_font_fiber (gpointer user_data)
{
    LoadAssetData    *data = user_data;
    g_autoptr(GError) error = NULL;
    GrlFont          *font;

    font = lrg_asset_manager_load_font (data->manager, data->name, data->size, &error);
    if (font == NULL)
    {
        return dex_future_new_for_error (g_steal_pointer (&error));
    }

    return dex_future_new_for_pointer (g_object_ref (font));
}

static DexFuture *
load_sound_fiber (gpointer user_data)
{
    LoadAssetData    *data = user_data;
    g_autoptr(GError) error = NULL;
    GrlSound         *sound;

    sound = lrg_asset_manager_load_sound (data->manager, data->name, &error);
    if (sound == NULL)
    {
        return dex_future_new_for_error (g_steal_pointer (&error));
    }

    return dex_future_new_for_pointer (g_object_ref (sound));
}

static DexFuture *
load_music_fiber (gpointer user_data)
{
    LoadAssetData    *data = user_data;
    g_autoptr(GError) error = NULL;
    GrlMusic         *music;

    music = lrg_asset_manager_load_music (data->manager, data->name, &error);
    if (music == NULL)
    {
        return dex_future_new_for_error (g_steal_pointer (&error));
    }

    return dex_future_new_for_pointer (g_object_ref (music));
}

/* ==========================================================================
 * Public API - Asynchronous Loading
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
DexFuture *
lrg_asset_manager_load_texture_async (LrgAssetManager *self,
                                      const gchar     *name)
{
    LoadAssetData *data;

    g_return_val_if_fail (LRG_IS_ASSET_MANAGER (self), NULL);
    g_return_val_if_fail (name != NULL, NULL);

    data = g_new0 (LoadAssetData, 1);
    data->manager = g_object_ref (self);
    data->name = g_strdup (name);

    return dex_scheduler_spawn (NULL,
                                0,
                                load_texture_fiber,
                                data,
                                (GDestroyNotify)load_asset_data_free);
}

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
DexFuture *
lrg_asset_manager_load_font_async (LrgAssetManager *self,
                                   const gchar     *name,
                                   gint             size)
{
    LoadAssetData *data;

    g_return_val_if_fail (LRG_IS_ASSET_MANAGER (self), NULL);
    g_return_val_if_fail (name != NULL, NULL);
    g_return_val_if_fail (size > 0, NULL);

    data = g_new0 (LoadAssetData, 1);
    data->manager = g_object_ref (self);
    data->name = g_strdup (name);
    data->size = size;

    return dex_scheduler_spawn (NULL,
                                0,
                                load_font_fiber,
                                data,
                                (GDestroyNotify)load_asset_data_free);
}

/**
 * lrg_asset_manager_load_sound_async:
 * @self: an #LrgAssetManager
 * @name: relative path to sound file
 *
 * Asynchronously loads a sound effect.
 *
 * Returns: (transfer full): A #DexFuture that resolves to a #GrlSound
 */
DexFuture *
lrg_asset_manager_load_sound_async (LrgAssetManager *self,
                                    const gchar     *name)
{
    LoadAssetData *data;

    g_return_val_if_fail (LRG_IS_ASSET_MANAGER (self), NULL);
    g_return_val_if_fail (name != NULL, NULL);

    data = g_new0 (LoadAssetData, 1);
    data->manager = g_object_ref (self);
    data->name = g_strdup (name);

    return dex_scheduler_spawn (NULL,
                                0,
                                load_sound_fiber,
                                data,
                                (GDestroyNotify)load_asset_data_free);
}

/**
 * lrg_asset_manager_load_music_async:
 * @self: an #LrgAssetManager
 * @name: relative path to music file
 *
 * Asynchronously loads a music track.
 *
 * Returns: (transfer full): A #DexFuture that resolves to a #GrlMusic
 */
DexFuture *
lrg_asset_manager_load_music_async (LrgAssetManager *self,
                                    const gchar     *name)
{
    LoadAssetData *data;

    g_return_val_if_fail (LRG_IS_ASSET_MANAGER (self), NULL);
    g_return_val_if_fail (name != NULL, NULL);

    data = g_new0 (LoadAssetData, 1);
    data->manager = g_object_ref (self);
    data->name = g_strdup (name);

    return dex_scheduler_spawn (NULL,
                                0,
                                load_music_fiber,
                                data,
                                (GDestroyNotify)load_asset_data_free);
}
#endif /* LRG_HAS_LIBDEX */

/* ==========================================================================
 * Public API - Cache Management
 * ========================================================================== */

/**
 * lrg_asset_manager_unload:
 * @self: an #LrgAssetManager
 * @name: the asset name to unload
 *
 * Removes an asset from all caches.
 *
 * Returns: %TRUE if the asset was in a cache and removed
 */
gboolean
lrg_asset_manager_unload (LrgAssetManager *self,
                          const gchar     *name)
{
    LrgAssetManagerPrivate *priv;
    gboolean                removed = FALSE;

    g_return_val_if_fail (LRG_IS_ASSET_MANAGER (self), FALSE);
    g_return_val_if_fail (name != NULL, FALSE);

    priv = lrg_asset_manager_get_instance_private (self);

    /* Try to remove from each cache */
    if (g_hash_table_remove (priv->texture_cache, name))
    {
        removed = TRUE;
    }

    if (g_hash_table_remove (priv->sound_cache, name))
    {
        removed = TRUE;
    }

    if (g_hash_table_remove (priv->music_cache, name))
    {
        removed = TRUE;
    }

    /* For fonts, we need to check keys that start with name: */
    {
        GHashTableIter iter;
        gpointer       key;
        GList         *to_remove = NULL;
        GList         *l;

        g_hash_table_iter_init (&iter, priv->font_cache);
        while (g_hash_table_iter_next (&iter, &key, NULL))
        {
            const gchar *cache_key = key;

            if (g_str_has_prefix (cache_key, name) &&
                cache_key[strlen (name)] == ':')
            {
                to_remove = g_list_prepend (to_remove, g_strdup (cache_key));
            }
        }

        for (l = to_remove; l != NULL; l = l->next)
        {
            g_hash_table_remove (priv->font_cache, l->data);
            removed = TRUE;
        }

        g_list_free_full (to_remove, g_free);
    }

    if (removed)
    {
        lrg_debug (LRG_LOG_DOMAIN_CORE,
                   "Unloaded asset: %s",
                   name);
    }

    return removed;
}

/**
 * lrg_asset_manager_unload_all:
 * @self: an #LrgAssetManager
 *
 * Clears all cached assets.
 */
void
lrg_asset_manager_unload_all (LrgAssetManager *self)
{
    LrgAssetManagerPrivate *priv;

    g_return_if_fail (LRG_IS_ASSET_MANAGER (self));

    priv = lrg_asset_manager_get_instance_private (self);

    g_hash_table_remove_all (priv->texture_cache);
    g_hash_table_remove_all (priv->font_cache);
    g_hash_table_remove_all (priv->sound_cache);
    g_hash_table_remove_all (priv->music_cache);

    lrg_debug (LRG_LOG_DOMAIN_CORE, "Unloaded all cached assets");
}

/**
 * lrg_asset_manager_is_cached:
 * @self: an #LrgAssetManager
 * @name: the asset name to check
 *
 * Checks if an asset is currently in any cache.
 *
 * Returns: %TRUE if the asset is cached
 */
gboolean
lrg_asset_manager_is_cached (LrgAssetManager *self,
                             const gchar     *name)
{
    LrgAssetManagerPrivate *priv;

    g_return_val_if_fail (LRG_IS_ASSET_MANAGER (self), FALSE);
    g_return_val_if_fail (name != NULL, FALSE);

    priv = lrg_asset_manager_get_instance_private (self);

    /* Check each cache */
    if (g_hash_table_contains (priv->texture_cache, name))
    {
        return TRUE;
    }

    if (g_hash_table_contains (priv->sound_cache, name))
    {
        return TRUE;
    }

    if (g_hash_table_contains (priv->music_cache, name))
    {
        return TRUE;
    }

    /* For fonts, check if any key starts with name: */
    {
        GHashTableIter iter;
        gpointer       key;

        g_hash_table_iter_init (&iter, priv->font_cache);
        while (g_hash_table_iter_next (&iter, &key, NULL))
        {
            const gchar *cache_key = key;

            if (g_str_has_prefix (cache_key, name) &&
                cache_key[strlen (name)] == ':')
            {
                return TRUE;
            }
        }
    }

    return FALSE;
}

/**
 * lrg_asset_manager_get_texture_cache_size:
 * @self: an #LrgAssetManager
 *
 * Gets the number of cached textures.
 *
 * Returns: The texture cache size
 */
guint
lrg_asset_manager_get_texture_cache_size (LrgAssetManager *self)
{
    LrgAssetManagerPrivate *priv;

    g_return_val_if_fail (LRG_IS_ASSET_MANAGER (self), 0);

    priv = lrg_asset_manager_get_instance_private (self);

    return g_hash_table_size (priv->texture_cache);
}

/**
 * lrg_asset_manager_get_font_cache_size:
 * @self: an #LrgAssetManager
 *
 * Gets the number of cached fonts.
 *
 * Returns: The font cache size
 */
guint
lrg_asset_manager_get_font_cache_size (LrgAssetManager *self)
{
    LrgAssetManagerPrivate *priv;

    g_return_val_if_fail (LRG_IS_ASSET_MANAGER (self), 0);

    priv = lrg_asset_manager_get_instance_private (self);

    return g_hash_table_size (priv->font_cache);
}

/**
 * lrg_asset_manager_get_sound_cache_size:
 * @self: an #LrgAssetManager
 *
 * Gets the number of cached sounds.
 *
 * Returns: The sound cache size
 */
guint
lrg_asset_manager_get_sound_cache_size (LrgAssetManager *self)
{
    LrgAssetManagerPrivate *priv;

    g_return_val_if_fail (LRG_IS_ASSET_MANAGER (self), 0);

    priv = lrg_asset_manager_get_instance_private (self);

    return g_hash_table_size (priv->sound_cache);
}

/**
 * lrg_asset_manager_get_music_cache_size:
 * @self: an #LrgAssetManager
 *
 * Gets the number of cached music tracks.
 *
 * Returns: The music cache size
 */
guint
lrg_asset_manager_get_music_cache_size (LrgAssetManager *self)
{
    LrgAssetManagerPrivate *priv;

    g_return_val_if_fail (LRG_IS_ASSET_MANAGER (self), 0);

    priv = lrg_asset_manager_get_instance_private (self);

    return g_hash_table_size (priv->music_cache);
}
