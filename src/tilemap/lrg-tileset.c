/* lrg-tileset.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Texture atlas for tilemap rendering.
 */

#include "config.h"

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_TILEMAP

#include "lrg-tileset.h"
#include "../lrg-log.h"

#include <gio/gio.h>

/* ==========================================================================
 * Private Data
 * ========================================================================== */

struct _LrgTileset
{
    GObject     parent_instance;

    GrlTexture *texture;
    guint       tile_width;
    guint       tile_height;
    guint       columns;
    guint       rows;
    guint       tile_count;

    /* Per-tile properties, indexed by tile_id */
    GArray     *tile_properties;
};

G_DEFINE_TYPE (LrgTileset, lrg_tileset, G_TYPE_OBJECT)

enum
{
    PROP_0,
    PROP_TEXTURE,
    PROP_TILE_WIDTH,
    PROP_TILE_HEIGHT,
    PROP_COLUMNS,
    PROP_ROWS,
    PROP_TILE_COUNT,
    N_PROPS
};

static GParamSpec *properties[N_PROPS];

/* ==========================================================================
 * GObject Implementation
 * ========================================================================== */

static void
lrg_tileset_finalize (GObject *object)
{
    LrgTileset *self = LRG_TILESET (object);

    g_clear_object (&self->texture);
    g_clear_pointer (&self->tile_properties, g_array_unref);

    G_OBJECT_CLASS (lrg_tileset_parent_class)->finalize (object);
}

static void
lrg_tileset_get_property (GObject    *object,
                          guint       prop_id,
                          GValue     *value,
                          GParamSpec *pspec)
{
    LrgTileset *self = LRG_TILESET (object);

    switch (prop_id)
    {
    case PROP_TEXTURE:
        g_value_set_object (value, self->texture);
        break;
    case PROP_TILE_WIDTH:
        g_value_set_uint (value, self->tile_width);
        break;
    case PROP_TILE_HEIGHT:
        g_value_set_uint (value, self->tile_height);
        break;
    case PROP_COLUMNS:
        g_value_set_uint (value, self->columns);
        break;
    case PROP_ROWS:
        g_value_set_uint (value, self->rows);
        break;
    case PROP_TILE_COUNT:
        g_value_set_uint (value, self->tile_count);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_tileset_class_init (LrgTilesetClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->finalize = lrg_tileset_finalize;
    object_class->get_property = lrg_tileset_get_property;

    /**
     * LrgTileset:texture:
     *
     * The source texture atlas.
     */
    properties[PROP_TEXTURE] =
        g_param_spec_object ("texture",
                             "Texture",
                             "The source texture atlas",
                             GRL_TYPE_TEXTURE,
                             G_PARAM_READABLE |
                             G_PARAM_STATIC_STRINGS);

    /**
     * LrgTileset:tile-width:
     *
     * The width of each tile in pixels.
     */
    properties[PROP_TILE_WIDTH] =
        g_param_spec_uint ("tile-width",
                           "Tile Width",
                           "Width of each tile in pixels",
                           1, G_MAXUINT, 16,
                           G_PARAM_READABLE |
                           G_PARAM_STATIC_STRINGS);

    /**
     * LrgTileset:tile-height:
     *
     * The height of each tile in pixels.
     */
    properties[PROP_TILE_HEIGHT] =
        g_param_spec_uint ("tile-height",
                           "Tile Height",
                           "Height of each tile in pixels",
                           1, G_MAXUINT, 16,
                           G_PARAM_READABLE |
                           G_PARAM_STATIC_STRINGS);

    /**
     * LrgTileset:columns:
     *
     * The number of tile columns in the tileset.
     */
    properties[PROP_COLUMNS] =
        g_param_spec_uint ("columns",
                           "Columns",
                           "Number of tile columns",
                           0, G_MAXUINT, 0,
                           G_PARAM_READABLE |
                           G_PARAM_STATIC_STRINGS);

    /**
     * LrgTileset:rows:
     *
     * The number of tile rows in the tileset.
     */
    properties[PROP_ROWS] =
        g_param_spec_uint ("rows",
                           "Rows",
                           "Number of tile rows",
                           0, G_MAXUINT, 0,
                           G_PARAM_READABLE |
                           G_PARAM_STATIC_STRINGS);

    /**
     * LrgTileset:tile-count:
     *
     * The total number of tiles in the tileset.
     */
    properties[PROP_TILE_COUNT] =
        g_param_spec_uint ("tile-count",
                           "Tile Count",
                           "Total number of tiles",
                           0, G_MAXUINT, 0,
                           G_PARAM_READABLE |
                           G_PARAM_STATIC_STRINGS);

    g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
lrg_tileset_init (LrgTileset *self)
{
    self->texture = NULL;
    self->tile_width = 16;
    self->tile_height = 16;
    self->columns = 0;
    self->rows = 0;
    self->tile_count = 0;
    self->tile_properties = NULL;
}

/* ==========================================================================
 * Construction
 * ========================================================================== */

/**
 * lrg_tileset_new:
 * @texture: (transfer none): the source texture atlas
 * @tile_width: width of each tile in pixels
 * @tile_height: height of each tile in pixels
 *
 * Creates a new tileset from a texture atlas. The texture is divided
 * into a grid of tiles based on the specified tile dimensions.
 *
 * The number of columns is calculated from the texture width divided
 * by @tile_width. Tiles are numbered left-to-right, top-to-bottom,
 * starting from 0.
 *
 * Returns: (transfer full): A new #LrgTileset
 */
LrgTileset *
lrg_tileset_new (GrlTexture *texture,
                 guint       tile_width,
                 guint       tile_height)
{
    LrgTileset *self;
    gint        tex_width;
    gint        tex_height;

    g_return_val_if_fail (GRL_IS_TEXTURE (texture), NULL);
    g_return_val_if_fail (tile_width > 0, NULL);
    g_return_val_if_fail (tile_height > 0, NULL);

    self = g_object_new (LRG_TYPE_TILESET, NULL);

    self->texture = g_object_ref (texture);
    self->tile_width = tile_width;
    self->tile_height = tile_height;

    /* Calculate grid dimensions */
    tex_width = grl_texture_get_width (texture);
    tex_height = grl_texture_get_height (texture);

    self->columns = (guint)(tex_width / (gint)tile_width);
    self->rows = (guint)(tex_height / (gint)tile_height);
    self->tile_count = self->columns * self->rows;

    /* Initialize tile properties array */
    self->tile_properties = g_array_sized_new (FALSE, TRUE,
                                               sizeof (LrgTileProperty),
                                               self->tile_count);
    g_array_set_size (self->tile_properties, self->tile_count);

    lrg_log_debug ("Created tileset: %ux%u tiles (%ux%u each) from %dx%d texture",
               self->columns, self->rows,
               tile_width, tile_height,
               tex_width, tex_height);

    return self;
}

/**
 * lrg_tileset_new_from_file:
 * @path: path to the image file
 * @tile_width: width of each tile in pixels
 * @tile_height: height of each tile in pixels
 * @error: (nullable): return location for error, or %NULL
 *
 * Creates a new tileset by loading a texture from a file.
 *
 * Returns: (transfer full) (nullable): A new #LrgTileset, or %NULL on error
 */
LrgTileset *
lrg_tileset_new_from_file (const gchar  *path,
                           guint         tile_width,
                           guint         tile_height,
                           GError      **error)
{
    g_autoptr(GrlTexture) texture = NULL;
    LrgTileset           *tileset;

    g_return_val_if_fail (path != NULL, NULL);
    g_return_val_if_fail (tile_width > 0, NULL);
    g_return_val_if_fail (tile_height > 0, NULL);

    texture = grl_texture_new_from_file (path);
    if (texture == NULL || !grl_texture_is_valid (texture))
    {
        g_set_error (error,
                     G_IO_ERROR,
                     G_IO_ERROR_FAILED,
                     "Failed to load texture from '%s'",
                     path);
        return NULL;
    }

    tileset = lrg_tileset_new (texture, tile_width, tile_height);

    return tileset;
}

/* ==========================================================================
 * Accessors
 * ========================================================================== */

/**
 * lrg_tileset_get_texture:
 * @self: an #LrgTileset
 *
 * Gets the underlying texture atlas.
 *
 * Returns: (transfer none): The texture
 */
GrlTexture *
lrg_tileset_get_texture (LrgTileset *self)
{
    g_return_val_if_fail (LRG_IS_TILESET (self), NULL);

    return self->texture;
}

/**
 * lrg_tileset_get_tile_width:
 * @self: an #LrgTileset
 *
 * Gets the width of each tile in pixels.
 *
 * Returns: The tile width
 */
guint
lrg_tileset_get_tile_width (LrgTileset *self)
{
    g_return_val_if_fail (LRG_IS_TILESET (self), 0);

    return self->tile_width;
}

/**
 * lrg_tileset_get_tile_height:
 * @self: an #LrgTileset
 *
 * Gets the height of each tile in pixels.
 *
 * Returns: The tile height
 */
guint
lrg_tileset_get_tile_height (LrgTileset *self)
{
    g_return_val_if_fail (LRG_IS_TILESET (self), 0);

    return self->tile_height;
}

/**
 * lrg_tileset_get_columns:
 * @self: an #LrgTileset
 *
 * Gets the number of tile columns in the tileset.
 *
 * Returns: The column count
 */
guint
lrg_tileset_get_columns (LrgTileset *self)
{
    g_return_val_if_fail (LRG_IS_TILESET (self), 0);

    return self->columns;
}

/**
 * lrg_tileset_get_rows:
 * @self: an #LrgTileset
 *
 * Gets the number of tile rows in the tileset.
 *
 * Returns: The row count
 */
guint
lrg_tileset_get_rows (LrgTileset *self)
{
    g_return_val_if_fail (LRG_IS_TILESET (self), 0);

    return self->rows;
}

/**
 * lrg_tileset_get_tile_count:
 * @self: an #LrgTileset
 *
 * Gets the total number of tiles in the tileset.
 *
 * Returns: The tile count
 */
guint
lrg_tileset_get_tile_count (LrgTileset *self)
{
    g_return_val_if_fail (LRG_IS_TILESET (self), 0);

    return self->tile_count;
}

/* ==========================================================================
 * Tile Rectangles
 * ========================================================================== */

/**
 * lrg_tileset_get_tile_rect:
 * @self: an #LrgTileset
 * @tile_id: the tile index (0-based)
 *
 * Gets the source rectangle for a specific tile within the texture.
 * This rectangle can be used with grl_draw_texture_rec() to render
 * the tile.
 *
 * Returns: (transfer full) (nullable): The tile source rectangle,
 *          or %NULL if @tile_id is out of bounds
 */
GrlRectangle *
lrg_tileset_get_tile_rect (LrgTileset *self,
                           guint       tile_id)
{
    GrlRectangle *rect;
    guint         col;
    guint         row;

    g_return_val_if_fail (LRG_IS_TILESET (self), NULL);

    if (tile_id >= self->tile_count)
    {
        return NULL;
    }

    col = tile_id % self->columns;
    row = tile_id / self->columns;

    rect = grl_rectangle_new ((gfloat)(col * self->tile_width),
                              (gfloat)(row * self->tile_height),
                              (gfloat)self->tile_width,
                              (gfloat)self->tile_height);

    return rect;
}

/**
 * lrg_tileset_get_tile_rect_to:
 * @self: an #LrgTileset
 * @tile_id: the tile index (0-based)
 * @out_rect: (out caller-allocates): location to store the rectangle
 *
 * Gets the source rectangle for a specific tile, storing it in
 * a caller-provided rectangle. This avoids allocation.
 *
 * Returns: %TRUE if successful, %FALSE if @tile_id is out of bounds
 */
gboolean
lrg_tileset_get_tile_rect_to (LrgTileset   *self,
                              guint         tile_id,
                              GrlRectangle *out_rect)
{
    guint col;
    guint row;

    g_return_val_if_fail (LRG_IS_TILESET (self), FALSE);
    g_return_val_if_fail (out_rect != NULL, FALSE);

    if (tile_id >= self->tile_count)
    {
        return FALSE;
    }

    col = tile_id % self->columns;
    row = tile_id / self->columns;

    out_rect->x = (gfloat)(col * self->tile_width);
    out_rect->y = (gfloat)(row * self->tile_height);
    out_rect->width = (gfloat)self->tile_width;
    out_rect->height = (gfloat)self->tile_height;

    return TRUE;
}

/* ==========================================================================
 * Tile Properties
 * ========================================================================== */

/**
 * lrg_tileset_get_tile_properties:
 * @self: an #LrgTileset
 * @tile_id: the tile index (0-based)
 *
 * Gets the property flags for a specific tile.
 *
 * Returns: The tile property flags
 */
LrgTileProperty
lrg_tileset_get_tile_properties (LrgTileset *self,
                                 guint       tile_id)
{
    g_return_val_if_fail (LRG_IS_TILESET (self), LRG_TILE_PROPERTY_NONE);

    if (tile_id >= self->tile_count)
    {
        return LRG_TILE_PROPERTY_NONE;
    }

    return g_array_index (self->tile_properties, LrgTileProperty, tile_id);
}

/**
 * lrg_tileset_set_tile_properties:
 * @self: an #LrgTileset
 * @tile_id: the tile index (0-based)
 * @properties: the property flags to set
 *
 * Sets the property flags for a specific tile.
 */
void
lrg_tileset_set_tile_properties (LrgTileset      *self,
                                 guint            tile_id,
                                 LrgTileProperty  properties)
{
    g_return_if_fail (LRG_IS_TILESET (self));

    if (tile_id >= self->tile_count)
    {
        g_warning ("Tile ID %u out of bounds (max %u)", tile_id, self->tile_count);
        return;
    }

    g_array_index (self->tile_properties, LrgTileProperty, tile_id) = properties;
}

/**
 * lrg_tileset_tile_has_property:
 * @self: an #LrgTileset
 * @tile_id: the tile index (0-based)
 * @property: the property to check
 *
 * Checks if a tile has a specific property flag set.
 *
 * Returns: %TRUE if the tile has the property
 */
gboolean
lrg_tileset_tile_has_property (LrgTileset      *self,
                               guint            tile_id,
                               LrgTileProperty  property)
{
    LrgTileProperty props;

    g_return_val_if_fail (LRG_IS_TILESET (self), FALSE);

    if (tile_id >= self->tile_count)
    {
        return FALSE;
    }

    props = g_array_index (self->tile_properties, LrgTileProperty, tile_id);

    return (props & property) == property;
}
