/* lrg-tilemap.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Multi-layer tilemap with rendering support.
 */

#include "config.h"

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_TILEMAP

#include "lrg-tilemap.h"
#include "../lrg-log.h"

/* ==========================================================================
 * Private Data
 * ========================================================================== */

typedef struct
{
    LrgTileset *tileset;
    GList      *layers;     /* List of LrgTilemapLayer* */
} LrgTilemapPrivate;

G_DEFINE_TYPE_WITH_PRIVATE (LrgTilemap, lrg_tilemap, G_TYPE_OBJECT)

enum
{
    PROP_0,
    PROP_TILESET,
    N_PROPS
};

static GParamSpec *properties[N_PROPS];

enum
{
    SIGNAL_TILE_CHANGED,
    N_SIGNALS
};

static guint signals[N_SIGNALS];

/* ==========================================================================
 * GObject Implementation
 * ========================================================================== */

static void
lrg_tilemap_dispose (GObject *object)
{
    LrgTilemap        *self = LRG_TILEMAP (object);
    LrgTilemapPrivate *priv = lrg_tilemap_get_instance_private (self);

    g_clear_object (&priv->tileset);

    if (priv->layers != NULL)
    {
        g_list_free_full (priv->layers, g_object_unref);
        priv->layers = NULL;
    }

    G_OBJECT_CLASS (lrg_tilemap_parent_class)->dispose (object);
}

static void
lrg_tilemap_get_property (GObject    *object,
                          guint       prop_id,
                          GValue     *value,
                          GParamSpec *pspec)
{
    LrgTilemap        *self = LRG_TILEMAP (object);
    LrgTilemapPrivate *priv = lrg_tilemap_get_instance_private (self);

    switch (prop_id)
    {
    case PROP_TILESET:
        g_value_set_object (value, priv->tileset);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_tilemap_set_property (GObject      *object,
                          guint         prop_id,
                          const GValue *value,
                          GParamSpec   *pspec)
{
    LrgTilemap *self = LRG_TILEMAP (object);

    switch (prop_id)
    {
    case PROP_TILESET:
        lrg_tilemap_set_tileset (self, g_value_get_object (value));
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_tilemap_class_init (LrgTilemapClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->dispose = lrg_tilemap_dispose;
    object_class->get_property = lrg_tilemap_get_property;
    object_class->set_property = lrg_tilemap_set_property;

    /**
     * LrgTilemap:tileset:
     *
     * The tileset used for rendering tiles.
     */
    properties[PROP_TILESET] =
        g_param_spec_object ("tileset",
                             "Tileset",
                             "The tileset for rendering",
                             LRG_TYPE_TILESET,
                             G_PARAM_READWRITE |
                             G_PARAM_STATIC_STRINGS |
                             G_PARAM_EXPLICIT_NOTIFY);

    g_object_class_install_properties (object_class, N_PROPS, properties);

    /**
     * LrgTilemap::tile-changed:
     * @self: the tilemap
     * @layer_index: the layer where the tile changed
     * @x: the tile X coordinate
     * @y: the tile Y coordinate
     * @old_tile: the previous tile ID
     * @new_tile: the new tile ID
     *
     * Emitted when a tile value changes in any layer.
     */
    signals[SIGNAL_TILE_CHANGED] =
        g_signal_new ("tile-changed",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0, NULL, NULL, NULL,
                      G_TYPE_NONE, 5,
                      G_TYPE_UINT,  /* layer_index */
                      G_TYPE_UINT,  /* x */
                      G_TYPE_UINT,  /* y */
                      G_TYPE_UINT,  /* old_tile */
                      G_TYPE_UINT); /* new_tile */
}

static void
lrg_tilemap_init (LrgTilemap *self)
{
    LrgTilemapPrivate *priv = lrg_tilemap_get_instance_private (self);

    priv->tileset = NULL;
    priv->layers = NULL;
}

/* ==========================================================================
 * Construction
 * ========================================================================== */

/**
 * lrg_tilemap_new:
 * @tileset: (transfer none): the tileset to use for rendering
 *
 * Creates a new tilemap with the specified tileset.
 * The tilemap starts with no layers.
 *
 * Returns: (transfer full): A new #LrgTilemap
 */
LrgTilemap *
lrg_tilemap_new (LrgTileset *tileset)
{
    g_return_val_if_fail (LRG_IS_TILESET (tileset), NULL);

    return g_object_new (LRG_TYPE_TILEMAP,
                         "tileset", tileset,
                         NULL);
}

/* ==========================================================================
 * Tileset
 * ========================================================================== */

/**
 * lrg_tilemap_get_tileset:
 * @self: an #LrgTilemap
 *
 * Gets the tileset used for rendering.
 *
 * Returns: (transfer none): The tileset
 */
LrgTileset *
lrg_tilemap_get_tileset (LrgTilemap *self)
{
    LrgTilemapPrivate *priv;

    g_return_val_if_fail (LRG_IS_TILEMAP (self), NULL);

    priv = lrg_tilemap_get_instance_private (self);

    return priv->tileset;
}

/**
 * lrg_tilemap_set_tileset:
 * @self: an #LrgTilemap
 * @tileset: (transfer none): the tileset
 *
 * Sets the tileset used for rendering.
 */
void
lrg_tilemap_set_tileset (LrgTilemap *self,
                         LrgTileset *tileset)
{
    LrgTilemapPrivate *priv;

    g_return_if_fail (LRG_IS_TILEMAP (self));
    g_return_if_fail (tileset == NULL || LRG_IS_TILESET (tileset));

    priv = lrg_tilemap_get_instance_private (self);

    if (g_set_object (&priv->tileset, tileset))
    {
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_TILESET]);
    }
}

/* ==========================================================================
 * Layer Management
 * ========================================================================== */

/**
 * lrg_tilemap_add_layer:
 * @self: an #LrgTilemap
 * @layer: (transfer none): the layer to add
 *
 * Adds a layer to the tilemap. Layers are rendered in the order
 * they are added (first added = rendered first = behind).
 */
void
lrg_tilemap_add_layer (LrgTilemap      *self,
                       LrgTilemapLayer *layer)
{
    LrgTilemapPrivate *priv;

    g_return_if_fail (LRG_IS_TILEMAP (self));
    g_return_if_fail (LRG_IS_TILEMAP_LAYER (layer));

    priv = lrg_tilemap_get_instance_private (self);

    priv->layers = g_list_append (priv->layers, g_object_ref (layer));

    lrg_log_debug ("Added layer '%s' to tilemap (total: %u)",
               lrg_tilemap_layer_get_name (layer) ?: "(unnamed)",
               g_list_length (priv->layers));
}

/**
 * lrg_tilemap_insert_layer:
 * @self: an #LrgTilemap
 * @layer: (transfer none): the layer to insert
 * @index: the position to insert at
 *
 * Inserts a layer at a specific position. Layers with lower indices
 * are rendered first (behind layers with higher indices).
 */
void
lrg_tilemap_insert_layer (LrgTilemap      *self,
                          LrgTilemapLayer *layer,
                          guint            index)
{
    LrgTilemapPrivate *priv;

    g_return_if_fail (LRG_IS_TILEMAP (self));
    g_return_if_fail (LRG_IS_TILEMAP_LAYER (layer));

    priv = lrg_tilemap_get_instance_private (self);

    priv->layers = g_list_insert (priv->layers, g_object_ref (layer), (gint)index);
}

/**
 * lrg_tilemap_remove_layer:
 * @self: an #LrgTilemap
 * @layer: the layer to remove
 *
 * Removes a layer from the tilemap.
 */
void
lrg_tilemap_remove_layer (LrgTilemap      *self,
                          LrgTilemapLayer *layer)
{
    LrgTilemapPrivate *priv;
    GList             *link;

    g_return_if_fail (LRG_IS_TILEMAP (self));
    g_return_if_fail (LRG_IS_TILEMAP_LAYER (layer));

    priv = lrg_tilemap_get_instance_private (self);

    link = g_list_find (priv->layers, layer);
    if (link != NULL)
    {
        priv->layers = g_list_delete_link (priv->layers, link);
        g_object_unref (layer);
    }
}

/**
 * lrg_tilemap_remove_layer_at:
 * @self: an #LrgTilemap
 * @index: the layer index to remove
 *
 * Removes the layer at the specified index.
 */
void
lrg_tilemap_remove_layer_at (LrgTilemap *self,
                             guint       index)
{
    LrgTilemapPrivate *priv;
    GList             *link;

    g_return_if_fail (LRG_IS_TILEMAP (self));

    priv = lrg_tilemap_get_instance_private (self);

    link = g_list_nth (priv->layers, index);
    if (link != NULL)
    {
        g_object_unref (link->data);
        priv->layers = g_list_delete_link (priv->layers, link);
    }
}

/**
 * lrg_tilemap_get_layer:
 * @self: an #LrgTilemap
 * @index: the layer index
 *
 * Gets a layer by index.
 *
 * Returns: (transfer none) (nullable): The layer, or %NULL if out of bounds
 */
LrgTilemapLayer *
lrg_tilemap_get_layer (LrgTilemap *self,
                       guint       index)
{
    LrgTilemapPrivate *priv;

    g_return_val_if_fail (LRG_IS_TILEMAP (self), NULL);

    priv = lrg_tilemap_get_instance_private (self);

    return (LrgTilemapLayer *)g_list_nth_data (priv->layers, index);
}

/**
 * lrg_tilemap_get_layer_by_name:
 * @self: an #LrgTilemap
 * @name: the layer name
 *
 * Finds a layer by name.
 *
 * Returns: (transfer none) (nullable): The layer, or %NULL if not found
 */
LrgTilemapLayer *
lrg_tilemap_get_layer_by_name (LrgTilemap  *self,
                               const gchar *name)
{
    LrgTilemapPrivate *priv;
    GList             *l;

    g_return_val_if_fail (LRG_IS_TILEMAP (self), NULL);
    g_return_val_if_fail (name != NULL, NULL);

    priv = lrg_tilemap_get_instance_private (self);

    for (l = priv->layers; l != NULL; l = l->next)
    {
        LrgTilemapLayer *layer = LRG_TILEMAP_LAYER (l->data);
        const gchar     *layer_name = lrg_tilemap_layer_get_name (layer);

        if (layer_name != NULL && g_strcmp0 (layer_name, name) == 0)
        {
            return layer;
        }
    }

    return NULL;
}

/**
 * lrg_tilemap_get_layer_count:
 * @self: an #LrgTilemap
 *
 * Gets the number of layers in the tilemap.
 *
 * Returns: The layer count
 */
guint
lrg_tilemap_get_layer_count (LrgTilemap *self)
{
    LrgTilemapPrivate *priv;

    g_return_val_if_fail (LRG_IS_TILEMAP (self), 0);

    priv = lrg_tilemap_get_instance_private (self);

    return g_list_length (priv->layers);
}

/**
 * lrg_tilemap_get_layers:
 * @self: an #LrgTilemap
 *
 * Gets all layers in the tilemap.
 *
 * Returns: (element-type LrgTilemapLayer) (transfer none): The layers list
 */
GList *
lrg_tilemap_get_layers (LrgTilemap *self)
{
    LrgTilemapPrivate *priv;

    g_return_val_if_fail (LRG_IS_TILEMAP (self), NULL);

    priv = lrg_tilemap_get_instance_private (self);

    return priv->layers;
}

/* ==========================================================================
 * Internal Drawing Helper
 * ========================================================================== */

static void
draw_layer_internal (LrgTilemap      *self,
                     LrgTilemapLayer *layer,
                     gfloat           offset_x,
                     gfloat           offset_y)
{
    LrgTilemapPrivate *priv = lrg_tilemap_get_instance_private (self);
    GrlTexture        *texture;
    guint              layer_width;
    guint              layer_height;
    guint              tile_width;
    guint              tile_height;
    guint              x;
    guint              y;
    gfloat             opacity;
    GrlColor           tint;

    if (!lrg_tilemap_layer_get_visible (layer))
    {
        return;
    }

    if (priv->tileset == NULL)
    {
        return;
    }

    texture = lrg_tileset_get_texture (priv->tileset);
    if (texture == NULL)
    {
        return;
    }

    tile_width = lrg_tileset_get_tile_width (priv->tileset);
    tile_height = lrg_tileset_get_tile_height (priv->tileset);
    layer_width = lrg_tilemap_layer_get_width (layer);
    layer_height = lrg_tilemap_layer_get_height (layer);

    /* Apply opacity to tint */
    opacity = lrg_tilemap_layer_get_opacity (layer);
    tint.r = 255;
    tint.g = 255;
    tint.b = 255;
    tint.a = (guint8)(opacity * 255.0f);

    /* Draw each tile */
    for (y = 0; y < layer_height; y++)
    {
        for (x = 0; x < layer_width; x++)
        {
            guint         tile_id;
            GrlRectangle  source;
            GrlRectangle  dest;
            GrlVector2    origin;

            tile_id = lrg_tilemap_layer_get_tile (layer, x, y);

            /* Skip empty tiles */
            if (tile_id == LRG_TILEMAP_EMPTY_TILE)
            {
                continue;
            }

            /* Tile IDs start at 1 in most formats, tileset indices start at 0 */
            if (!lrg_tileset_get_tile_rect_to (priv->tileset, tile_id - 1, &source))
            {
                continue;
            }

            dest.x = offset_x + (gfloat)(x * tile_width);
            dest.y = offset_y + (gfloat)(y * tile_height);
            dest.width = (gfloat)tile_width;
            dest.height = (gfloat)tile_height;

            origin.x = 0.0f;
            origin.y = 0.0f;

            grl_draw_texture_pro (texture, &source, &dest, &origin, 0.0f, &tint);
        }
    }
}

/* ==========================================================================
 * Rendering
 * ========================================================================== */

/**
 * lrg_tilemap_draw:
 * @self: an #LrgTilemap
 *
 * Draws all visible layers of the tilemap at position (0, 0).
 */
void
lrg_tilemap_draw (LrgTilemap *self)
{
    lrg_tilemap_draw_at (self, 0.0f, 0.0f);
}

/**
 * lrg_tilemap_draw_at:
 * @self: an #LrgTilemap
 * @x: the X position to draw at
 * @y: the Y position to draw at
 *
 * Draws all visible layers at the specified position.
 */
void
lrg_tilemap_draw_at (LrgTilemap *self,
                     gfloat      x,
                     gfloat      y)
{
    LrgTilemapPrivate *priv;
    GList             *l;

    g_return_if_fail (LRG_IS_TILEMAP (self));

    priv = lrg_tilemap_get_instance_private (self);

    for (l = priv->layers; l != NULL; l = l->next)
    {
        draw_layer_internal (self, LRG_TILEMAP_LAYER (l->data), x, y);
    }
}

/**
 * lrg_tilemap_draw_with_camera:
 * @self: an #LrgTilemap
 * @camera: the camera for view transformation and parallax
 *
 * Draws all visible layers using a camera for view transformation.
 * Parallax scrolling is applied based on each layer's parallax settings.
 */
void
lrg_tilemap_draw_with_camera (LrgTilemap  *self,
                              GrlCamera2D *camera)
{
    LrgTilemapPrivate    *priv;
    GList                *l;
    g_autoptr(GrlVector2) target = NULL;
    gfloat                cam_x;
    gfloat                cam_y;

    g_return_if_fail (LRG_IS_TILEMAP (self));
    g_return_if_fail (GRL_IS_CAMERA2D (camera));

    priv = lrg_tilemap_get_instance_private (self);

    /* Get camera target as the base offset */
    target = grl_camera2d_get_target (camera);
    cam_x = target->x;
    cam_y = target->y;

    for (l = priv->layers; l != NULL; l = l->next)
    {
        LrgTilemapLayer *layer = LRG_TILEMAP_LAYER (l->data);
        gfloat           parallax_x;
        gfloat           parallax_y;
        gfloat           offset_x;
        gfloat           offset_y;

        parallax_x = lrg_tilemap_layer_get_parallax_x (layer);
        parallax_y = lrg_tilemap_layer_get_parallax_y (layer);

        /* Apply parallax: layers with factor < 1.0 move slower than camera */
        offset_x = -cam_x * parallax_x;
        offset_y = -cam_y * parallax_y;

        draw_layer_internal (self, layer, offset_x, offset_y);
    }
}

/**
 * lrg_tilemap_draw_layer:
 * @self: an #LrgTilemap
 * @layer_index: the layer to draw
 * @x: the X position
 * @y: the Y position
 *
 * Draws a specific layer at the given position.
 */
void
lrg_tilemap_draw_layer (LrgTilemap *self,
                        guint       layer_index,
                        gfloat      x,
                        gfloat      y)
{
    LrgTilemapLayer *layer;

    g_return_if_fail (LRG_IS_TILEMAP (self));

    layer = lrg_tilemap_get_layer (self, layer_index);
    if (layer != NULL)
    {
        draw_layer_internal (self, layer, x, y);
    }
}

/* ==========================================================================
 * Collision Queries
 * ========================================================================== */

/**
 * lrg_tilemap_is_solid:
 * @self: an #LrgTilemap
 * @tile_x: the tile X coordinate
 * @tile_y: the tile Y coordinate
 *
 * Checks if any collision-enabled layer has a solid tile at the position.
 *
 * Returns: %TRUE if there is a solid tile
 */
gboolean
lrg_tilemap_is_solid (LrgTilemap *self,
                      guint       tile_x,
                      guint       tile_y)
{
    LrgTilemapPrivate *priv;
    GList             *l;

    g_return_val_if_fail (LRG_IS_TILEMAP (self), FALSE);

    priv = lrg_tilemap_get_instance_private (self);

    if (priv->tileset == NULL)
    {
        return FALSE;
    }

    for (l = priv->layers; l != NULL; l = l->next)
    {
        LrgTilemapLayer *layer = LRG_TILEMAP_LAYER (l->data);
        guint            tile_id;

        if (!lrg_tilemap_layer_get_collision_enabled (layer))
        {
            continue;
        }

        tile_id = lrg_tilemap_layer_get_tile (layer, tile_x, tile_y);
        if (tile_id == LRG_TILEMAP_EMPTY_TILE)
        {
            continue;
        }

        /* Check if tile has SOLID property (tile_id - 1 for 0-based index) */
        if (lrg_tileset_tile_has_property (priv->tileset, tile_id - 1,
                                           LRG_TILE_PROPERTY_SOLID))
        {
            return TRUE;
        }
    }

    return FALSE;
}

/**
 * lrg_tilemap_is_solid_at:
 * @self: an #LrgTilemap
 * @world_x: the world X coordinate in pixels
 * @world_y: the world Y coordinate in pixels
 *
 * Checks if there is a solid tile at the world position.
 *
 * Returns: %TRUE if there is a solid tile
 */
gboolean
lrg_tilemap_is_solid_at (LrgTilemap *self,
                         gfloat      world_x,
                         gfloat      world_y)
{
    guint tile_x;
    guint tile_y;

    g_return_val_if_fail (LRG_IS_TILEMAP (self), FALSE);

    lrg_tilemap_world_to_tile (self, world_x, world_y, &tile_x, &tile_y);

    return lrg_tilemap_is_solid (self, tile_x, tile_y);
}

/**
 * lrg_tilemap_get_tile_at:
 * @self: an #LrgTilemap
 * @layer_index: the layer to query
 * @world_x: the world X coordinate in pixels
 * @world_y: the world Y coordinate in pixels
 *
 * Gets the tile ID at a world position for a specific layer.
 *
 * Returns: The tile ID, or %LRG_TILEMAP_EMPTY_TILE if out of bounds
 */
guint
lrg_tilemap_get_tile_at (LrgTilemap *self,
                         guint       layer_index,
                         gfloat      world_x,
                         gfloat      world_y)
{
    LrgTilemapLayer *layer;
    guint            tile_x;
    guint            tile_y;

    g_return_val_if_fail (LRG_IS_TILEMAP (self), LRG_TILEMAP_EMPTY_TILE);

    layer = lrg_tilemap_get_layer (self, layer_index);
    if (layer == NULL)
    {
        return LRG_TILEMAP_EMPTY_TILE;
    }

    lrg_tilemap_world_to_tile (self, world_x, world_y, &tile_x, &tile_y);

    return lrg_tilemap_layer_get_tile (layer, tile_x, tile_y);
}

/* ==========================================================================
 * World Bounds
 * ========================================================================== */

/**
 * lrg_tilemap_get_world_bounds:
 * @self: an #LrgTilemap
 *
 * Gets the world bounds of the tilemap in pixels.
 * Uses the dimensions of the first layer or returns empty if no layers.
 *
 * Returns: (transfer full): The bounds rectangle
 */
GrlRectangle *
lrg_tilemap_get_world_bounds (LrgTilemap *self)
{
    g_return_val_if_fail (LRG_IS_TILEMAP (self), NULL);

    return grl_rectangle_new (0.0f, 0.0f,
                              (gfloat)lrg_tilemap_get_pixel_width (self),
                              (gfloat)lrg_tilemap_get_pixel_height (self));
}

/**
 * lrg_tilemap_get_width:
 * @self: an #LrgTilemap
 *
 * Gets the tilemap width in tiles (from first layer).
 *
 * Returns: The width in tiles
 */
guint
lrg_tilemap_get_width (LrgTilemap *self)
{
    LrgTilemapLayer *layer;

    g_return_val_if_fail (LRG_IS_TILEMAP (self), 0);

    layer = lrg_tilemap_get_layer (self, 0);
    if (layer == NULL)
    {
        return 0;
    }

    return lrg_tilemap_layer_get_width (layer);
}

/**
 * lrg_tilemap_get_height:
 * @self: an #LrgTilemap
 *
 * Gets the tilemap height in tiles (from first layer).
 *
 * Returns: The height in tiles
 */
guint
lrg_tilemap_get_height (LrgTilemap *self)
{
    LrgTilemapLayer *layer;

    g_return_val_if_fail (LRG_IS_TILEMAP (self), 0);

    layer = lrg_tilemap_get_layer (self, 0);
    if (layer == NULL)
    {
        return 0;
    }

    return lrg_tilemap_layer_get_height (layer);
}

/**
 * lrg_tilemap_get_pixel_width:
 * @self: an #LrgTilemap
 *
 * Gets the tilemap width in pixels.
 *
 * Returns: The width in pixels
 */
guint
lrg_tilemap_get_pixel_width (LrgTilemap *self)
{
    LrgTilemapPrivate *priv;
    guint              tile_width;

    g_return_val_if_fail (LRG_IS_TILEMAP (self), 0);

    priv = lrg_tilemap_get_instance_private (self);

    if (priv->tileset == NULL)
    {
        return 0;
    }

    tile_width = lrg_tileset_get_tile_width (priv->tileset);

    return lrg_tilemap_get_width (self) * tile_width;
}

/**
 * lrg_tilemap_get_pixel_height:
 * @self: an #LrgTilemap
 *
 * Gets the tilemap height in pixels.
 *
 * Returns: The height in pixels
 */
guint
lrg_tilemap_get_pixel_height (LrgTilemap *self)
{
    LrgTilemapPrivate *priv;
    guint              tile_height;

    g_return_val_if_fail (LRG_IS_TILEMAP (self), 0);

    priv = lrg_tilemap_get_instance_private (self);

    if (priv->tileset == NULL)
    {
        return 0;
    }

    tile_height = lrg_tileset_get_tile_height (priv->tileset);

    return lrg_tilemap_get_height (self) * tile_height;
}

/* ==========================================================================
 * Coordinate Conversion
 * ========================================================================== */

/**
 * lrg_tilemap_world_to_tile:
 * @self: an #LrgTilemap
 * @world_x: the world X coordinate
 * @world_y: the world Y coordinate
 * @out_tile_x: (out): return location for tile X
 * @out_tile_y: (out): return location for tile Y
 *
 * Converts world coordinates to tile coordinates.
 */
void
lrg_tilemap_world_to_tile (LrgTilemap *self,
                           gfloat      world_x,
                           gfloat      world_y,
                           guint      *out_tile_x,
                           guint      *out_tile_y)
{
    LrgTilemapPrivate *priv;
    guint              tile_width;
    guint              tile_height;

    g_return_if_fail (LRG_IS_TILEMAP (self));
    g_return_if_fail (out_tile_x != NULL);
    g_return_if_fail (out_tile_y != NULL);

    priv = lrg_tilemap_get_instance_private (self);

    if (priv->tileset == NULL)
    {
        *out_tile_x = 0;
        *out_tile_y = 0;
        return;
    }

    tile_width = lrg_tileset_get_tile_width (priv->tileset);
    tile_height = lrg_tileset_get_tile_height (priv->tileset);

    /* Handle negative coordinates */
    if (world_x < 0.0f)
    {
        *out_tile_x = 0;
    }
    else
    {
        *out_tile_x = (guint)(world_x / (gfloat)tile_width);
    }

    if (world_y < 0.0f)
    {
        *out_tile_y = 0;
    }
    else
    {
        *out_tile_y = (guint)(world_y / (gfloat)tile_height);
    }
}

/**
 * lrg_tilemap_tile_to_world:
 * @self: an #LrgTilemap
 * @tile_x: the tile X coordinate
 * @tile_y: the tile Y coordinate
 * @out_world_x: (out): return location for world X
 * @out_world_y: (out): return location for world Y
 *
 * Converts tile coordinates to world coordinates (top-left corner of tile).
 */
void
lrg_tilemap_tile_to_world (LrgTilemap *self,
                           guint       tile_x,
                           guint       tile_y,
                           gfloat     *out_world_x,
                           gfloat     *out_world_y)
{
    LrgTilemapPrivate *priv;
    guint              tile_width;
    guint              tile_height;

    g_return_if_fail (LRG_IS_TILEMAP (self));
    g_return_if_fail (out_world_x != NULL);
    g_return_if_fail (out_world_y != NULL);

    priv = lrg_tilemap_get_instance_private (self);

    if (priv->tileset == NULL)
    {
        *out_world_x = 0.0f;
        *out_world_y = 0.0f;
        return;
    }

    tile_width = lrg_tileset_get_tile_width (priv->tileset);
    tile_height = lrg_tileset_get_tile_height (priv->tileset);

    *out_world_x = (gfloat)(tile_x * tile_width);
    *out_world_y = (gfloat)(tile_y * tile_height);
}
