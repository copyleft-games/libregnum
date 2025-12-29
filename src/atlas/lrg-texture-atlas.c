/* lrg-texture-atlas.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Texture atlas implementation.
 */

#include "config.h"

#include <gio/gio.h>

#include "lrg-texture-atlas.h"

/**
 * LrgTextureAtlas:
 *
 * A texture atlas for efficient sprite rendering.
 *
 * #LrgTextureAtlas manages a collection of named regions within a
 * single texture, allowing efficient batch rendering of sprites.
 *
 * Since: 1.0
 */
struct _LrgTextureAtlas
{
    GObject parent_instance;

    /* Identity */
    gchar *name;

    /* Texture info */
    gchar *texture_path;
    gint   width;
    gint   height;

    /* Regions: name -> LrgAtlasRegion */
    GHashTable *regions;
};

G_DEFINE_FINAL_TYPE (LrgTextureAtlas, lrg_texture_atlas, G_TYPE_OBJECT)

enum
{
    PROP_0,
    PROP_NAME,
    PROP_TEXTURE_PATH,
    PROP_WIDTH,
    PROP_HEIGHT,
    PROP_REGION_COUNT,
    N_PROPS
};

static GParamSpec *properties[N_PROPS];

/* GObject implementation */

static void
lrg_texture_atlas_finalize (GObject *object)
{
    LrgTextureAtlas *self = LRG_TEXTURE_ATLAS (object);

    g_clear_pointer (&self->name, g_free);
    g_clear_pointer (&self->texture_path, g_free);
    g_clear_pointer (&self->regions, g_hash_table_unref);

    G_OBJECT_CLASS (lrg_texture_atlas_parent_class)->finalize (object);
}

static void
lrg_texture_atlas_get_property (GObject    *object,
                                guint       prop_id,
                                GValue     *value,
                                GParamSpec *pspec)
{
    LrgTextureAtlas *self = LRG_TEXTURE_ATLAS (object);

    switch (prop_id)
    {
    case PROP_NAME:
        g_value_set_string (value, self->name);
        break;
    case PROP_TEXTURE_PATH:
        g_value_set_string (value, self->texture_path);
        break;
    case PROP_WIDTH:
        g_value_set_int (value, self->width);
        break;
    case PROP_HEIGHT:
        g_value_set_int (value, self->height);
        break;
    case PROP_REGION_COUNT:
        g_value_set_uint (value, g_hash_table_size (self->regions));
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

static void
lrg_texture_atlas_set_property (GObject      *object,
                                guint         prop_id,
                                const GValue *value,
                                GParamSpec   *pspec)
{
    LrgTextureAtlas *self = LRG_TEXTURE_ATLAS (object);

    switch (prop_id)
    {
    case PROP_NAME:
        g_free (self->name);
        self->name = g_value_dup_string (value);
        break;
    case PROP_TEXTURE_PATH:
        lrg_texture_atlas_set_texture_path (self, g_value_get_string (value));
        break;
    case PROP_WIDTH:
        lrg_texture_atlas_set_width (self, g_value_get_int (value));
        break;
    case PROP_HEIGHT:
        lrg_texture_atlas_set_height (self, g_value_get_int (value));
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

static void
lrg_texture_atlas_class_init (LrgTextureAtlasClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->finalize     = lrg_texture_atlas_finalize;
    object_class->get_property = lrg_texture_atlas_get_property;
    object_class->set_property = lrg_texture_atlas_set_property;

    /**
     * LrgTextureAtlas:name:
     *
     * The name identifier for the atlas.
     *
     * Since: 1.0
     */
    properties[PROP_NAME] =
        g_param_spec_string ("name",
                             "Name",
                             "Name identifier for the atlas",
                             NULL,
                             G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY |
                             G_PARAM_STATIC_STRINGS);

    /**
     * LrgTextureAtlas:texture-path:
     *
     * Path to the texture file.
     *
     * Since: 1.0
     */
    properties[PROP_TEXTURE_PATH] =
        g_param_spec_string ("texture-path",
                             "Texture Path",
                             "Path to the texture file",
                             NULL,
                             G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    /**
     * LrgTextureAtlas:width:
     *
     * Width of the atlas texture in pixels.
     *
     * Since: 1.0
     */
    properties[PROP_WIDTH] =
        g_param_spec_int ("width",
                          "Width",
                          "Width of the atlas texture",
                          0, G_MAXINT, 0,
                          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    /**
     * LrgTextureAtlas:height:
     *
     * Height of the atlas texture in pixels.
     *
     * Since: 1.0
     */
    properties[PROP_HEIGHT] =
        g_param_spec_int ("height",
                          "Height",
                          "Height of the atlas texture",
                          0, G_MAXINT, 0,
                          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    /**
     * LrgTextureAtlas:region-count:
     *
     * Number of regions in the atlas.
     *
     * Since: 1.0
     */
    properties[PROP_REGION_COUNT] =
        g_param_spec_uint ("region-count",
                           "Region Count",
                           "Number of regions in the atlas",
                           0, G_MAXUINT, 0,
                           G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);

    g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
lrg_texture_atlas_init (LrgTextureAtlas *self)
{
    self->regions = g_hash_table_new_full (g_str_hash, g_str_equal,
                                           g_free,
                                           (GDestroyNotify)lrg_atlas_region_free);
    self->width  = 0;
    self->height = 0;
}

/* Public API */

/**
 * lrg_texture_atlas_new:
 * @name: Name identifier for the atlas
 *
 * Creates a new empty texture atlas.
 *
 * Returns: (transfer full): A new #LrgTextureAtlas
 *
 * Since: 1.0
 */
LrgTextureAtlas *
lrg_texture_atlas_new (const gchar *name)
{
    return g_object_new (LRG_TYPE_TEXTURE_ATLAS,
                         "name", name,
                         NULL);
}

/**
 * lrg_texture_atlas_new_from_file:
 * @path: Path to the atlas definition file (YAML)
 * @error: (nullable): Return location for error
 *
 * Creates a texture atlas by loading a definition file.
 *
 * Returns: (transfer full) (nullable): A new #LrgTextureAtlas or %NULL on error
 *
 * Since: 1.0
 */
LrgTextureAtlas *
lrg_texture_atlas_new_from_file (const gchar  *path,
                                 GError      **error)
{
    /* TODO: Implement YAML loading using yaml-glib */
    g_set_error (error,
                 G_IO_ERROR,
                 G_IO_ERROR_NOT_SUPPORTED,
                 "Atlas file loading not yet implemented");
    return NULL;
}

/**
 * lrg_texture_atlas_get_name:
 * @self: A #LrgTextureAtlas
 *
 * Gets the name of the atlas.
 *
 * Returns: (transfer none) (nullable): The atlas name
 *
 * Since: 1.0
 */
const gchar *
lrg_texture_atlas_get_name (LrgTextureAtlas *self)
{
    g_return_val_if_fail (LRG_IS_TEXTURE_ATLAS (self), NULL);
    return self->name;
}

/**
 * lrg_texture_atlas_get_texture_path:
 * @self: A #LrgTextureAtlas
 *
 * Gets the path to the texture file.
 *
 * Returns: (transfer none) (nullable): The texture path
 *
 * Since: 1.0
 */
const gchar *
lrg_texture_atlas_get_texture_path (LrgTextureAtlas *self)
{
    g_return_val_if_fail (LRG_IS_TEXTURE_ATLAS (self), NULL);
    return self->texture_path;
}

/**
 * lrg_texture_atlas_set_texture_path:
 * @self: A #LrgTextureAtlas
 * @path: Path to the texture file
 *
 * Sets the path to the texture file.
 *
 * Since: 1.0
 */
void
lrg_texture_atlas_set_texture_path (LrgTextureAtlas *self,
                                    const gchar     *path)
{
    g_return_if_fail (LRG_IS_TEXTURE_ATLAS (self));

    if (g_strcmp0 (self->texture_path, path) != 0)
    {
        g_free (self->texture_path);
        self->texture_path = g_strdup (path);
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_TEXTURE_PATH]);
    }
}

/**
 * lrg_texture_atlas_get_width:
 * @self: A #LrgTextureAtlas
 *
 * Gets the width of the atlas texture.
 *
 * Returns: Width in pixels
 *
 * Since: 1.0
 */
gint
lrg_texture_atlas_get_width (LrgTextureAtlas *self)
{
    g_return_val_if_fail (LRG_IS_TEXTURE_ATLAS (self), 0);
    return self->width;
}

/**
 * lrg_texture_atlas_set_width:
 * @self: A #LrgTextureAtlas
 * @width: Width in pixels
 *
 * Sets the width of the atlas texture.
 *
 * Since: 1.0
 */
void
lrg_texture_atlas_set_width (LrgTextureAtlas *self,
                             gint             width)
{
    g_return_if_fail (LRG_IS_TEXTURE_ATLAS (self));
    g_return_if_fail (width >= 0);

    if (self->width != width)
    {
        self->width = width;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_WIDTH]);
    }
}

/**
 * lrg_texture_atlas_get_height:
 * @self: A #LrgTextureAtlas
 *
 * Gets the height of the atlas texture.
 *
 * Returns: Height in pixels
 *
 * Since: 1.0
 */
gint
lrg_texture_atlas_get_height (LrgTextureAtlas *self)
{
    g_return_val_if_fail (LRG_IS_TEXTURE_ATLAS (self), 0);
    return self->height;
}

/**
 * lrg_texture_atlas_set_height:
 * @self: A #LrgTextureAtlas
 * @height: Height in pixels
 *
 * Sets the height of the atlas texture.
 *
 * Since: 1.0
 */
void
lrg_texture_atlas_set_height (LrgTextureAtlas *self,
                              gint             height)
{
    g_return_if_fail (LRG_IS_TEXTURE_ATLAS (self));
    g_return_if_fail (height >= 0);

    if (self->height != height)
    {
        self->height = height;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_HEIGHT]);
    }
}

/**
 * lrg_texture_atlas_set_size:
 * @self: A #LrgTextureAtlas
 * @width: Width in pixels
 * @height: Height in pixels
 *
 * Sets both dimensions of the atlas texture.
 *
 * Since: 1.0
 */
void
lrg_texture_atlas_set_size (LrgTextureAtlas *self,
                            gint             width,
                            gint             height)
{
    g_return_if_fail (LRG_IS_TEXTURE_ATLAS (self));

    g_object_freeze_notify (G_OBJECT (self));
    lrg_texture_atlas_set_width (self, width);
    lrg_texture_atlas_set_height (self, height);
    g_object_thaw_notify (G_OBJECT (self));
}

/**
 * lrg_texture_atlas_add_region:
 * @self: A #LrgTextureAtlas
 * @region: The region to add
 *
 * Adds a region to the atlas. The atlas takes ownership of the region.
 *
 * Since: 1.0
 */
void
lrg_texture_atlas_add_region (LrgTextureAtlas *self,
                              LrgAtlasRegion  *region)
{
    const gchar *name;

    g_return_if_fail (LRG_IS_TEXTURE_ATLAS (self));
    g_return_if_fail (region != NULL);

    name = lrg_atlas_region_get_name (region);
    g_return_if_fail (name != NULL);

    g_hash_table_insert (self->regions, g_strdup (name), region);
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_REGION_COUNT]);
}

/**
 * lrg_texture_atlas_add_region_rect:
 * @self: A #LrgTextureAtlas
 * @name: Name of the region
 * @x: X position
 * @y: Y position
 * @width: Width
 * @height: Height
 *
 * Convenience function to add a region by rectangle.
 *
 * Returns: (transfer none): The added region
 *
 * Since: 1.0
 */
LrgAtlasRegion *
lrg_texture_atlas_add_region_rect (LrgTextureAtlas *self,
                                   const gchar     *name,
                                   gint             x,
                                   gint             y,
                                   gint             width,
                                   gint             height)
{
    LrgAtlasRegion *region;

    g_return_val_if_fail (LRG_IS_TEXTURE_ATLAS (self), NULL);
    g_return_val_if_fail (name != NULL, NULL);

    region = lrg_atlas_region_new (name, x, y, width, height);

    /* Calculate UVs if we have dimensions */
    if (self->width > 0 && self->height > 0)
        lrg_atlas_region_calculate_uv (region, self->width, self->height);

    lrg_texture_atlas_add_region (self, region);

    return region;
}

/**
 * lrg_texture_atlas_remove_region:
 * @self: A #LrgTextureAtlas
 * @name: Name of the region to remove
 *
 * Removes a region from the atlas.
 *
 * Returns: %TRUE if the region was found and removed
 *
 * Since: 1.0
 */
gboolean
lrg_texture_atlas_remove_region (LrgTextureAtlas *self,
                                 const gchar     *name)
{
    gboolean removed;

    g_return_val_if_fail (LRG_IS_TEXTURE_ATLAS (self), FALSE);
    g_return_val_if_fail (name != NULL, FALSE);

    removed = g_hash_table_remove (self->regions, name);

    if (removed)
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_REGION_COUNT]);

    return removed;
}

/**
 * lrg_texture_atlas_get_region:
 * @self: A #LrgTextureAtlas
 * @name: Name of the region
 *
 * Gets a region by name.
 *
 * Returns: (transfer none) (nullable): The region, or %NULL if not found
 *
 * Since: 1.0
 */
LrgAtlasRegion *
lrg_texture_atlas_get_region (LrgTextureAtlas *self,
                              const gchar     *name)
{
    g_return_val_if_fail (LRG_IS_TEXTURE_ATLAS (self), NULL);
    g_return_val_if_fail (name != NULL, NULL);

    return g_hash_table_lookup (self->regions, name);
}

/**
 * lrg_texture_atlas_has_region:
 * @self: A #LrgTextureAtlas
 * @name: Name of the region
 *
 * Checks if a region exists.
 *
 * Returns: %TRUE if the region exists
 *
 * Since: 1.0
 */
gboolean
lrg_texture_atlas_has_region (LrgTextureAtlas *self,
                              const gchar     *name)
{
    g_return_val_if_fail (LRG_IS_TEXTURE_ATLAS (self), FALSE);
    g_return_val_if_fail (name != NULL, FALSE);

    return g_hash_table_contains (self->regions, name);
}

/**
 * lrg_texture_atlas_get_region_count:
 * @self: A #LrgTextureAtlas
 *
 * Gets the number of regions.
 *
 * Returns: The region count
 *
 * Since: 1.0
 */
guint
lrg_texture_atlas_get_region_count (LrgTextureAtlas *self)
{
    g_return_val_if_fail (LRG_IS_TEXTURE_ATLAS (self), 0);
    return g_hash_table_size (self->regions);
}

/**
 * lrg_texture_atlas_get_region_names:
 * @self: A #LrgTextureAtlas
 *
 * Gets all region names.
 *
 * Returns: (transfer full) (element-type utf8): Array of region names
 *
 * Since: 1.0
 */
GPtrArray *
lrg_texture_atlas_get_region_names (LrgTextureAtlas *self)
{
    GPtrArray      *names;
    GHashTableIter  iter;
    gpointer        key;

    g_return_val_if_fail (LRG_IS_TEXTURE_ATLAS (self), NULL);

    names = g_ptr_array_new_with_free_func (g_free);

    g_hash_table_iter_init (&iter, self->regions);
    while (g_hash_table_iter_next (&iter, &key, NULL))
    {
        g_ptr_array_add (names, g_strdup ((const gchar *)key));
    }

    return names;
}

/**
 * lrg_texture_atlas_clear_regions:
 * @self: A #LrgTextureAtlas
 *
 * Removes all regions from the atlas.
 *
 * Since: 1.0
 */
void
lrg_texture_atlas_clear_regions (LrgTextureAtlas *self)
{
    g_return_if_fail (LRG_IS_TEXTURE_ATLAS (self));

    g_hash_table_remove_all (self->regions);
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_REGION_COUNT]);
}

/**
 * lrg_texture_atlas_recalculate_uvs:
 * @self: A #LrgTextureAtlas
 *
 * Recalculates UV coordinates for all regions.
 *
 * Since: 1.0
 */
void
lrg_texture_atlas_recalculate_uvs (LrgTextureAtlas *self)
{
    GHashTableIter  iter;
    LrgAtlasRegion *region;

    g_return_if_fail (LRG_IS_TEXTURE_ATLAS (self));
    g_return_if_fail (self->width > 0 && self->height > 0);

    g_hash_table_iter_init (&iter, self->regions);
    while (g_hash_table_iter_next (&iter, NULL, (gpointer *)&region))
    {
        lrg_atlas_region_calculate_uv (region, self->width, self->height);
    }
}

/**
 * lrg_texture_atlas_save_to_file:
 * @self: A #LrgTextureAtlas
 * @path: Path to save to
 * @error: (nullable): Return location for error
 *
 * Saves the atlas definition to a YAML file.
 *
 * Returns: %TRUE on success
 *
 * Since: 1.0
 */
gboolean
lrg_texture_atlas_save_to_file (LrgTextureAtlas  *self,
                                const gchar      *path,
                                GError          **error)
{
    /* TODO: Implement YAML saving using yaml-glib */
    g_set_error (error,
                 G_IO_ERROR,
                 G_IO_ERROR_NOT_SUPPORTED,
                 "Atlas file saving not yet implemented");
    return FALSE;
}
