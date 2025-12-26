/* lrg-tilemap-layer.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Single layer of tile data for a tilemap.
 */

#include "config.h"

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_TILEMAP

#include "lrg-tilemap-layer.h"
#include "../lrg-log.h"

/* ==========================================================================
 * Private Data
 * ========================================================================== */

struct _LrgTilemapLayer
{
    GObject   parent_instance;

    guint    *tiles;           /* 2D array stored as 1D (row-major) */
    guint     width;
    guint     height;
    gsize     tile_count;

    gboolean  visible;
    gboolean  collision_enabled;
    gfloat    parallax_x;
    gfloat    parallax_y;
    gfloat    opacity;
    gchar    *name;
};

G_DEFINE_TYPE (LrgTilemapLayer, lrg_tilemap_layer, G_TYPE_OBJECT)

enum
{
    PROP_0,
    PROP_WIDTH,
    PROP_HEIGHT,
    PROP_VISIBLE,
    PROP_COLLISION_ENABLED,
    PROP_PARALLAX_X,
    PROP_PARALLAX_Y,
    PROP_OPACITY,
    PROP_NAME,
    N_PROPS
};

static GParamSpec *properties[N_PROPS];

/* ==========================================================================
 * GObject Implementation
 * ========================================================================== */

static void
lrg_tilemap_layer_finalize (GObject *object)
{
    LrgTilemapLayer *self = LRG_TILEMAP_LAYER (object);

    g_clear_pointer (&self->tiles, g_free);
    g_clear_pointer (&self->name, g_free);

    G_OBJECT_CLASS (lrg_tilemap_layer_parent_class)->finalize (object);
}

static void
lrg_tilemap_layer_get_property (GObject    *object,
                                guint       prop_id,
                                GValue     *value,
                                GParamSpec *pspec)
{
    LrgTilemapLayer *self = LRG_TILEMAP_LAYER (object);

    switch (prop_id)
    {
    case PROP_WIDTH:
        g_value_set_uint (value, self->width);
        break;
    case PROP_HEIGHT:
        g_value_set_uint (value, self->height);
        break;
    case PROP_VISIBLE:
        g_value_set_boolean (value, self->visible);
        break;
    case PROP_COLLISION_ENABLED:
        g_value_set_boolean (value, self->collision_enabled);
        break;
    case PROP_PARALLAX_X:
        g_value_set_float (value, self->parallax_x);
        break;
    case PROP_PARALLAX_Y:
        g_value_set_float (value, self->parallax_y);
        break;
    case PROP_OPACITY:
        g_value_set_float (value, self->opacity);
        break;
    case PROP_NAME:
        g_value_set_string (value, self->name);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_tilemap_layer_set_property (GObject      *object,
                                guint         prop_id,
                                const GValue *value,
                                GParamSpec   *pspec)
{
    LrgTilemapLayer *self = LRG_TILEMAP_LAYER (object);

    switch (prop_id)
    {
    case PROP_VISIBLE:
        lrg_tilemap_layer_set_visible (self, g_value_get_boolean (value));
        break;
    case PROP_COLLISION_ENABLED:
        lrg_tilemap_layer_set_collision_enabled (self, g_value_get_boolean (value));
        break;
    case PROP_PARALLAX_X:
        lrg_tilemap_layer_set_parallax_x (self, g_value_get_float (value));
        break;
    case PROP_PARALLAX_Y:
        lrg_tilemap_layer_set_parallax_y (self, g_value_get_float (value));
        break;
    case PROP_OPACITY:
        lrg_tilemap_layer_set_opacity (self, g_value_get_float (value));
        break;
    case PROP_NAME:
        lrg_tilemap_layer_set_name (self, g_value_get_string (value));
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_tilemap_layer_class_init (LrgTilemapLayerClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->finalize = lrg_tilemap_layer_finalize;
    object_class->get_property = lrg_tilemap_layer_get_property;
    object_class->set_property = lrg_tilemap_layer_set_property;

    /**
     * LrgTilemapLayer:width:
     *
     * The layer width in tiles.
     */
    properties[PROP_WIDTH] =
        g_param_spec_uint ("width",
                           "Width",
                           "Layer width in tiles",
                           1, G_MAXUINT, 1,
                           G_PARAM_READABLE |
                           G_PARAM_STATIC_STRINGS);

    /**
     * LrgTilemapLayer:height:
     *
     * The layer height in tiles.
     */
    properties[PROP_HEIGHT] =
        g_param_spec_uint ("height",
                           "Height",
                           "Layer height in tiles",
                           1, G_MAXUINT, 1,
                           G_PARAM_READABLE |
                           G_PARAM_STATIC_STRINGS);

    /**
     * LrgTilemapLayer:visible:
     *
     * Whether the layer is visible when rendering.
     */
    properties[PROP_VISIBLE] =
        g_param_spec_boolean ("visible",
                              "Visible",
                              "Whether the layer is visible",
                              TRUE,
                              G_PARAM_READWRITE |
                              G_PARAM_STATIC_STRINGS |
                              G_PARAM_EXPLICIT_NOTIFY);

    /**
     * LrgTilemapLayer:collision-enabled:
     *
     * Whether collision detection considers this layer.
     */
    properties[PROP_COLLISION_ENABLED] =
        g_param_spec_boolean ("collision-enabled",
                              "Collision Enabled",
                              "Whether collision detection is enabled",
                              FALSE,
                              G_PARAM_READWRITE |
                              G_PARAM_STATIC_STRINGS |
                              G_PARAM_EXPLICIT_NOTIFY);

    /**
     * LrgTilemapLayer:parallax-x:
     *
     * Horizontal parallax scrolling factor.
     */
    properties[PROP_PARALLAX_X] =
        g_param_spec_float ("parallax-x",
                            "Parallax X",
                            "Horizontal parallax factor",
                            0.0f, G_MAXFLOAT, 1.0f,
                            G_PARAM_READWRITE |
                            G_PARAM_STATIC_STRINGS |
                            G_PARAM_EXPLICIT_NOTIFY);

    /**
     * LrgTilemapLayer:parallax-y:
     *
     * Vertical parallax scrolling factor.
     */
    properties[PROP_PARALLAX_Y] =
        g_param_spec_float ("parallax-y",
                            "Parallax Y",
                            "Vertical parallax factor",
                            0.0f, G_MAXFLOAT, 1.0f,
                            G_PARAM_READWRITE |
                            G_PARAM_STATIC_STRINGS |
                            G_PARAM_EXPLICIT_NOTIFY);

    /**
     * LrgTilemapLayer:opacity:
     *
     * Layer opacity (0.0 = transparent, 1.0 = opaque).
     */
    properties[PROP_OPACITY] =
        g_param_spec_float ("opacity",
                            "Opacity",
                            "Layer opacity (0.0-1.0)",
                            0.0f, 1.0f, 1.0f,
                            G_PARAM_READWRITE |
                            G_PARAM_STATIC_STRINGS |
                            G_PARAM_EXPLICIT_NOTIFY);

    /**
     * LrgTilemapLayer:name:
     *
     * The layer name for identification.
     */
    properties[PROP_NAME] =
        g_param_spec_string ("name",
                             "Name",
                             "Layer name",
                             NULL,
                             G_PARAM_READWRITE |
                             G_PARAM_STATIC_STRINGS |
                             G_PARAM_EXPLICIT_NOTIFY);

    g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
lrg_tilemap_layer_init (LrgTilemapLayer *self)
{
    self->tiles = NULL;
    self->width = 0;
    self->height = 0;
    self->tile_count = 0;
    self->visible = TRUE;
    self->collision_enabled = TRUE;
    self->parallax_x = 1.0f;
    self->parallax_y = 1.0f;
    self->opacity = 1.0f;
    self->name = NULL;
}

/* ==========================================================================
 * Construction
 * ========================================================================== */

/**
 * lrg_tilemap_layer_new:
 * @width: the layer width in tiles
 * @height: the layer height in tiles
 *
 * Creates a new tilemap layer with all tiles initialized to
 * %LRG_TILEMAP_EMPTY_TILE (0).
 *
 * Returns: (transfer full): A new #LrgTilemapLayer
 */
LrgTilemapLayer *
lrg_tilemap_layer_new (guint width,
                       guint height)
{
    LrgTilemapLayer *self;

    g_return_val_if_fail (width > 0, NULL);
    g_return_val_if_fail (height > 0, NULL);

    self = g_object_new (LRG_TYPE_TILEMAP_LAYER, NULL);

    self->width = width;
    self->height = height;
    self->tile_count = (gsize)width * (gsize)height;

    /* Allocate tile array, initialized to 0 */
    self->tiles = g_new0 (guint, self->tile_count);

    lrg_log_debug ("Created tilemap layer: %ux%u tiles", width, height);

    return self;
}

/* ==========================================================================
 * Dimensions
 * ========================================================================== */

/**
 * lrg_tilemap_layer_get_width:
 * @self: an #LrgTilemapLayer
 *
 * Gets the layer width in tiles.
 *
 * Returns: The width
 */
guint
lrg_tilemap_layer_get_width (LrgTilemapLayer *self)
{
    g_return_val_if_fail (LRG_IS_TILEMAP_LAYER (self), 0);

    return self->width;
}

/**
 * lrg_tilemap_layer_get_height:
 * @self: an #LrgTilemapLayer
 *
 * Gets the layer height in tiles.
 *
 * Returns: The height
 */
guint
lrg_tilemap_layer_get_height (LrgTilemapLayer *self)
{
    g_return_val_if_fail (LRG_IS_TILEMAP_LAYER (self), 0);

    return self->height;
}

/* ==========================================================================
 * Tile Access
 * ========================================================================== */

/**
 * lrg_tilemap_layer_get_tile:
 * @self: an #LrgTilemapLayer
 * @x: the tile X coordinate
 * @y: the tile Y coordinate
 *
 * Gets the tile ID at the specified position.
 *
 * Returns: The tile ID, or %LRG_TILEMAP_EMPTY_TILE if out of bounds
 */
guint
lrg_tilemap_layer_get_tile (LrgTilemapLayer *self,
                            guint            x,
                            guint            y)
{
    gsize index;

    g_return_val_if_fail (LRG_IS_TILEMAP_LAYER (self), LRG_TILEMAP_EMPTY_TILE);

    if (x >= self->width || y >= self->height)
    {
        return LRG_TILEMAP_EMPTY_TILE;
    }

    index = (gsize)y * (gsize)self->width + (gsize)x;

    return self->tiles[index];
}

/**
 * lrg_tilemap_layer_set_tile:
 * @self: an #LrgTilemapLayer
 * @x: the tile X coordinate
 * @y: the tile Y coordinate
 * @tile_id: the tile ID to set
 *
 * Sets the tile ID at the specified position.
 * Does nothing if coordinates are out of bounds.
 */
void
lrg_tilemap_layer_set_tile (LrgTilemapLayer *self,
                            guint            x,
                            guint            y,
                            guint            tile_id)
{
    gsize index;

    g_return_if_fail (LRG_IS_TILEMAP_LAYER (self));

    if (x >= self->width || y >= self->height)
    {
        return;
    }

    index = (gsize)y * (gsize)self->width + (gsize)x;
    self->tiles[index] = tile_id;
}

/**
 * lrg_tilemap_layer_fill:
 * @self: an #LrgTilemapLayer
 * @tile_id: the tile ID to fill with
 *
 * Fills the entire layer with the specified tile ID.
 */
void
lrg_tilemap_layer_fill (LrgTilemapLayer *self,
                        guint            tile_id)
{
    gsize i;

    g_return_if_fail (LRG_IS_TILEMAP_LAYER (self));

    for (i = 0; i < self->tile_count; i++)
    {
        self->tiles[i] = tile_id;
    }
}

/**
 * lrg_tilemap_layer_fill_rect:
 * @self: an #LrgTilemapLayer
 * @x: the starting X coordinate
 * @y: the starting Y coordinate
 * @width: the rectangle width in tiles
 * @height: the rectangle height in tiles
 * @tile_id: the tile ID to fill with
 *
 * Fills a rectangular region with the specified tile ID.
 * Coordinates are clamped to layer bounds.
 */
void
lrg_tilemap_layer_fill_rect (LrgTilemapLayer *self,
                             guint            x,
                             guint            y,
                             guint            width,
                             guint            height,
                             guint            tile_id)
{
    guint end_x;
    guint end_y;
    guint ty;
    guint tx;

    g_return_if_fail (LRG_IS_TILEMAP_LAYER (self));

    /* Clamp to layer bounds */
    if (x >= self->width || y >= self->height)
    {
        return;
    }

    end_x = x + width;
    end_y = y + height;

    if (end_x > self->width)
    {
        end_x = self->width;
    }
    if (end_y > self->height)
    {
        end_y = self->height;
    }

    for (ty = y; ty < end_y; ty++)
    {
        for (tx = x; tx < end_x; tx++)
        {
            self->tiles[(gsize)ty * (gsize)self->width + (gsize)tx] = tile_id;
        }
    }
}

/**
 * lrg_tilemap_layer_clear:
 * @self: an #LrgTilemapLayer
 *
 * Clears the layer by setting all tiles to %LRG_TILEMAP_EMPTY_TILE.
 */
void
lrg_tilemap_layer_clear (LrgTilemapLayer *self)
{
    g_return_if_fail (LRG_IS_TILEMAP_LAYER (self));

    memset (self->tiles, 0, self->tile_count * sizeof (guint));
}

/* ==========================================================================
 * Layer Properties
 * ========================================================================== */

/**
 * lrg_tilemap_layer_get_visible:
 * @self: an #LrgTilemapLayer
 *
 * Gets whether the layer is visible.
 *
 * Returns: %TRUE if visible
 */
gboolean
lrg_tilemap_layer_get_visible (LrgTilemapLayer *self)
{
    g_return_val_if_fail (LRG_IS_TILEMAP_LAYER (self), FALSE);

    return self->visible;
}

/**
 * lrg_tilemap_layer_set_visible:
 * @self: an #LrgTilemapLayer
 * @visible: whether the layer is visible
 *
 * Sets whether the layer is visible.
 */
void
lrg_tilemap_layer_set_visible (LrgTilemapLayer *self,
                               gboolean         visible)
{
    g_return_if_fail (LRG_IS_TILEMAP_LAYER (self));

    visible = !!visible;
    if (self->visible != visible)
    {
        self->visible = visible;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_VISIBLE]);
    }
}

/**
 * lrg_tilemap_layer_get_collision_enabled:
 * @self: an #LrgTilemapLayer
 *
 * Gets whether collision detection is enabled for this layer.
 *
 * Returns: %TRUE if collision is enabled
 */
gboolean
lrg_tilemap_layer_get_collision_enabled (LrgTilemapLayer *self)
{
    g_return_val_if_fail (LRG_IS_TILEMAP_LAYER (self), FALSE);

    return self->collision_enabled;
}

/**
 * lrg_tilemap_layer_set_collision_enabled:
 * @self: an #LrgTilemapLayer
 * @enabled: whether collision is enabled
 *
 * Sets whether collision detection is enabled for this layer.
 */
void
lrg_tilemap_layer_set_collision_enabled (LrgTilemapLayer *self,
                                         gboolean         enabled)
{
    g_return_if_fail (LRG_IS_TILEMAP_LAYER (self));

    enabled = !!enabled;
    if (self->collision_enabled != enabled)
    {
        self->collision_enabled = enabled;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_COLLISION_ENABLED]);
    }
}

/**
 * lrg_tilemap_layer_get_parallax_x:
 * @self: an #LrgTilemapLayer
 *
 * Gets the horizontal parallax factor.
 * 1.0 = normal scrolling, 0.5 = half speed, 0.0 = stationary.
 *
 * Returns: The parallax factor
 */
gfloat
lrg_tilemap_layer_get_parallax_x (LrgTilemapLayer *self)
{
    g_return_val_if_fail (LRG_IS_TILEMAP_LAYER (self), 1.0f);

    return self->parallax_x;
}

/**
 * lrg_tilemap_layer_set_parallax_x:
 * @self: an #LrgTilemapLayer
 * @parallax: the parallax factor
 *
 * Sets the horizontal parallax factor.
 */
void
lrg_tilemap_layer_set_parallax_x (LrgTilemapLayer *self,
                                  gfloat           parallax)
{
    g_return_if_fail (LRG_IS_TILEMAP_LAYER (self));

    if (self->parallax_x != parallax)
    {
        self->parallax_x = parallax;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_PARALLAX_X]);
    }
}

/**
 * lrg_tilemap_layer_get_parallax_y:
 * @self: an #LrgTilemapLayer
 *
 * Gets the vertical parallax factor.
 *
 * Returns: The parallax factor
 */
gfloat
lrg_tilemap_layer_get_parallax_y (LrgTilemapLayer *self)
{
    g_return_val_if_fail (LRG_IS_TILEMAP_LAYER (self), 1.0f);

    return self->parallax_y;
}

/**
 * lrg_tilemap_layer_set_parallax_y:
 * @self: an #LrgTilemapLayer
 * @parallax: the parallax factor
 *
 * Sets the vertical parallax factor.
 */
void
lrg_tilemap_layer_set_parallax_y (LrgTilemapLayer *self,
                                  gfloat           parallax)
{
    g_return_if_fail (LRG_IS_TILEMAP_LAYER (self));

    if (self->parallax_y != parallax)
    {
        self->parallax_y = parallax;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_PARALLAX_Y]);
    }
}

/**
 * lrg_tilemap_layer_get_opacity:
 * @self: an #LrgTilemapLayer
 *
 * Gets the layer opacity (0.0 = transparent, 1.0 = opaque).
 *
 * Returns: The opacity value
 */
gfloat
lrg_tilemap_layer_get_opacity (LrgTilemapLayer *self)
{
    g_return_val_if_fail (LRG_IS_TILEMAP_LAYER (self), 1.0f);

    return self->opacity;
}

/**
 * lrg_tilemap_layer_set_opacity:
 * @self: an #LrgTilemapLayer
 * @opacity: the opacity value (0.0 to 1.0)
 *
 * Sets the layer opacity.
 */
void
lrg_tilemap_layer_set_opacity (LrgTilemapLayer *self,
                               gfloat           opacity)
{
    g_return_if_fail (LRG_IS_TILEMAP_LAYER (self));

    /* Clamp to valid range */
    if (opacity < 0.0f)
    {
        opacity = 0.0f;
    }
    else if (opacity > 1.0f)
    {
        opacity = 1.0f;
    }

    if (self->opacity != opacity)
    {
        self->opacity = opacity;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_OPACITY]);
    }
}

/**
 * lrg_tilemap_layer_get_name:
 * @self: an #LrgTilemapLayer
 *
 * Gets the layer name.
 *
 * Returns: (transfer none) (nullable): The layer name, or %NULL
 */
const gchar *
lrg_tilemap_layer_get_name (LrgTilemapLayer *self)
{
    g_return_val_if_fail (LRG_IS_TILEMAP_LAYER (self), NULL);

    return self->name;
}

/**
 * lrg_tilemap_layer_set_name:
 * @self: an #LrgTilemapLayer
 * @name: (nullable): the layer name
 *
 * Sets the layer name.
 */
void
lrg_tilemap_layer_set_name (LrgTilemapLayer *self,
                            const gchar     *name)
{
    g_return_if_fail (LRG_IS_TILEMAP_LAYER (self));

    if (g_strcmp0 (self->name, name) != 0)
    {
        g_free (self->name);
        self->name = g_strdup (name);
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_NAME]);
    }
}

/* ==========================================================================
 * Tile Data Access
 * ========================================================================== */

/**
 * lrg_tilemap_layer_get_tiles:
 * @self: an #LrgTilemapLayer
 * @out_len: (out) (optional): return location for tile count
 *
 * Gets direct access to the tile data array.
 * The array is stored in row-major order (y * width + x).
 *
 * Returns: (array length=out_len) (transfer none): The tile data
 */
const guint *
lrg_tilemap_layer_get_tiles (LrgTilemapLayer *self,
                             gsize           *out_len)
{
    g_return_val_if_fail (LRG_IS_TILEMAP_LAYER (self), NULL);

    if (out_len != NULL)
    {
        *out_len = self->tile_count;
    }

    return self->tiles;
}

/**
 * lrg_tilemap_layer_set_tiles:
 * @self: an #LrgTilemapLayer
 * @tiles: (array length=len): the tile data to copy
 * @len: the number of tiles (must match width * height)
 *
 * Sets all tile data from an array. The array must contain
 * exactly width * height elements in row-major order.
 *
 * Returns: %TRUE if successful, %FALSE if @len is incorrect
 */
gboolean
lrg_tilemap_layer_set_tiles (LrgTilemapLayer *self,
                             const guint     *tiles,
                             gsize            len)
{
    g_return_val_if_fail (LRG_IS_TILEMAP_LAYER (self), FALSE);
    g_return_val_if_fail (tiles != NULL, FALSE);

    if (len != self->tile_count)
    {
        lrg_log_warning ("Tile data length %" G_GSIZE_FORMAT " does not match layer size %" G_GSIZE_FORMAT,
                         len, self->tile_count);
        return FALSE;
    }

    memcpy (self->tiles, tiles, len * sizeof (guint));

    return TRUE;
}
