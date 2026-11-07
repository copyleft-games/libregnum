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
#include "../graphics/lrg-draw-cull-private.h"

#include <math.h>
#include <string.h>

/* ==========================================================================
 * Private Data
 * ========================================================================== */

typedef struct
{
    LrgTileset *tileset;
    GList      *layers;     /* List of LrgTilemapLayer* */

    /* 0.9.0: grid layout */
    LrgTilemapOrientation  orientation;
    LrgTilemapStaggerAxis  stagger_axis;
    LrgTilemapStaggerIndex stagger_index;
    guint                  hex_side_length;   /* 0: automatic */
    guint                  cell_width;        /* 0: tileset tile width */
    guint                  cell_height;       /* 0: tileset tile height */

    /* 0.9.0: animated tiles */
    guint64                anim_time_ms;
    gdouble                anim_remainder;    /* sub-millisecond carry */
} LrgTilemapPrivate;

G_DEFINE_TYPE_WITH_PRIVATE (LrgTilemap, lrg_tilemap, G_TYPE_OBJECT)

enum
{
    PROP_0,
    PROP_TILESET,
    PROP_ORIENTATION,
    PROP_STAGGER_AXIS,
    PROP_STAGGER_INDEX,
    PROP_HEX_SIDE_LENGTH,
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
    case PROP_ORIENTATION:
        g_value_set_enum (value, priv->orientation);
        break;
    case PROP_STAGGER_AXIS:
        g_value_set_enum (value, priv->stagger_axis);
        break;
    case PROP_STAGGER_INDEX:
        g_value_set_enum (value, priv->stagger_index);
        break;
    case PROP_HEX_SIDE_LENGTH:
        g_value_set_uint (value, priv->hex_side_length);
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
    case PROP_ORIENTATION:
        lrg_tilemap_set_orientation (self, g_value_get_enum (value));
        break;
    case PROP_STAGGER_AXIS:
        lrg_tilemap_set_stagger_axis (self, g_value_get_enum (value));
        break;
    case PROP_STAGGER_INDEX:
        lrg_tilemap_set_stagger_index (self, g_value_get_enum (value));
        break;
    case PROP_HEX_SIDE_LENGTH:
        lrg_tilemap_set_hex_side_length (self, g_value_get_uint (value));
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

    /**
     * LrgTilemap:orientation:
     *
     * How cells are laid out on screen (orthogonal, isometric, staggered
     * or hexagonal).
     */
    properties[PROP_ORIENTATION] =
        g_param_spec_enum ("orientation", NULL, NULL,
                           LRG_TYPE_TILEMAP_ORIENTATION, LRG_TILEMAP_ORIENTATION_ORTHOGONAL,
                           G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

    /**
     * LrgTilemap:stagger-axis:
     *
     * The axis staggered and hexagonal maps shift (y: rows, pointy-top
     * hexagons; x: columns, flat-top hexagons).
     */
    properties[PROP_STAGGER_AXIS] =
        g_param_spec_enum ("stagger-axis", NULL, NULL,
                           LRG_TYPE_TILEMAP_STAGGER_AXIS, LRG_TILEMAP_STAGGER_AXIS_Y,
                           G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

    /**
     * LrgTilemap:stagger-index:
     *
     * Whether the odd or the even rows (columns) are shifted.
     */
    properties[PROP_STAGGER_INDEX] =
        g_param_spec_enum ("stagger-index", NULL, NULL,
                           LRG_TYPE_TILEMAP_STAGGER_INDEX, LRG_TILEMAP_STAGGER_INDEX_ODD,
                           G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

    /**
     * LrgTilemap:hex-side-length:
     *
     * Length in pixels of the hexagon sides along the stagger axis; 0 is
     * half the cell size on that axis.
     */
    properties[PROP_HEX_SIDE_LENGTH] =
        g_param_spec_uint ("hex-side-length", NULL, NULL, 0, 65536, 0,
                           G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

    g_object_class_install_properties (object_class, N_PROPS, properties);

    /*
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
    priv->orientation = LRG_TILEMAP_ORIENTATION_ORTHOGONAL;
    priv->stagger_axis = LRG_TILEMAP_STAGGER_AXIS_Y;
    priv->stagger_index = LRG_TILEMAP_STAGGER_INDEX_ODD;
}

/* ==========================================================================
 * Construction
 * ========================================================================== */

/*
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

/*
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

/*
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

/*
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

/*
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

/*
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

/*
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

/*
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

/*
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

/*
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

/*
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
 * Grid geometry
 *
 * Grid gathers everything the cell maths needs.  Conventions follow Tiled:
 *
 * - orthogonal: cell (x, y) spans [x*cw, (x+1)*cw) x [y*ch, (y+1)*ch);
 * - isometric: the top corner of cell (x, y) is at
 *   ((x - y) * cw/2 + map_height * cw/2, (x + y) * ch/2), so the map's
 *   bounding box starts at (0, 0);
 * - staggered / hexagonal, stagger axis y: rows advance by
 *   row_step = side_offset + side, shifted rows move right by cw/2;
 *   stagger axis x: columns advance by col_step = side_offset + side,
 *   shifted columns move down by ch/2.  side is 0 for staggered maps
 *   (diamonds) and side_offset = (cell size on the axis - side) / 2.
 * ========================================================================== */

typedef struct
{
    LrgTilemapOrientation orientation;
    gboolean              axis_x;       /* stagger axis x */
    gboolean              odd;          /* odd rows/columns shifted */
    gfloat                cw;
    gfloat                ch;
    gfloat                side;
    gfloat                side_offset;
    guint                 map_w;
    guint                 map_h;
} Grid;

static void
grid_init (LrgTilemap *self,
           Grid       *g)
{
    LrgTilemapPrivate *priv = lrg_tilemap_get_instance_private (self);

    g->orientation = priv->orientation;
    g->axis_x = priv->stagger_axis == LRG_TILEMAP_STAGGER_AXIS_X;
    g->odd = priv->stagger_index == LRG_TILEMAP_STAGGER_INDEX_ODD;
    g->cw = (gfloat) MAX (lrg_tilemap_get_cell_width (self), 1u);
    g->ch = (gfloat) MAX (lrg_tilemap_get_cell_height (self), 1u);
    g->map_w = lrg_tilemap_get_width (self);
    g->map_h = lrg_tilemap_get_height (self);

    g->side = 0.0f;
    if (g->orientation == LRG_TILEMAP_ORIENTATION_HEXAGONAL)
    {
        gfloat along = g->axis_x ? g->cw : g->ch;

        g->side = priv->hex_side_length > 0 ? MIN ((gfloat) priv->hex_side_length, along)
                                            : along * 0.5f;
    }
    g->side_offset = ((g->axis_x ? g->cw : g->ch) - g->side) * 0.5f;
}

static gboolean
grid_is_staggered (const Grid *g)
{
    return g->orientation == LRG_TILEMAP_ORIENTATION_STAGGERED ||
           g->orientation == LRG_TILEMAP_ORIENTATION_HEXAGONAL;
}

/* is row/column @i shifted?  (i & 1 is right for negatives in two's complement) */
static gboolean
grid_shifted (const Grid *g,
              gint        i)
{
    return ((i & 1) != 0) == g->odd;
}

/* top-left corner of a cell's bounding box */
static void
grid_bbox (const Grid *g,
           gint        x,
           gint        y,
           gfloat     *bx,
           gfloat     *by)
{
    switch (g->orientation)
    {
    case LRG_TILEMAP_ORIENTATION_ISOMETRIC:
        *bx = (gfloat) (x - y) * g->cw * 0.5f + (gfloat) g->map_h * g->cw * 0.5f - g->cw * 0.5f;
        *by = (gfloat) (x + y) * g->ch * 0.5f;
        break;
    case LRG_TILEMAP_ORIENTATION_STAGGERED:
    case LRG_TILEMAP_ORIENTATION_HEXAGONAL:
        if (g->axis_x)
        {
            *bx = (gfloat) x * (g->side_offset + g->side);
            *by = (gfloat) y * g->ch + (grid_shifted (g, x) ? g->ch * 0.5f : 0.0f);
        }
        else
        {
            *bx = (gfloat) x * g->cw + (grid_shifted (g, y) ? g->cw * 0.5f : 0.0f);
            *by = (gfloat) y * (g->side_offset + g->side);
        }
        break;
    case LRG_TILEMAP_ORIENTATION_ORTHOGONAL:
    default:
        *bx = (gfloat) x * g->cw;
        *by = (gfloat) y * g->ch;
        break;
    }
}

/* the map's bounding box size */
static void
grid_map_size (const Grid *g,
               gfloat     *w,
               gfloat     *h)
{
    if (g->map_w == 0 || g->map_h == 0)
    {
        *w = 0.0f;
        *h = 0.0f;
        return;
    }
    switch (g->orientation)
    {
    case LRG_TILEMAP_ORIENTATION_ISOMETRIC:
        *w = (gfloat) (g->map_w + g->map_h) * g->cw * 0.5f;
        *h = (gfloat) (g->map_w + g->map_h) * g->ch * 0.5f;
        break;
    case LRG_TILEMAP_ORIENTATION_STAGGERED:
    case LRG_TILEMAP_ORIENTATION_HEXAGONAL:
        if (g->axis_x)
        {
            *w = (gfloat) g->map_w * (g->side_offset + g->side) + g->side_offset;
            *h = (gfloat) g->map_h * g->ch + (g->map_w > 1 ? g->ch * 0.5f : 0.0f);
        }
        else
        {
            *w = (gfloat) g->map_w * g->cw + (g->map_h > 1 ? g->cw * 0.5f : 0.0f);
            *h = (gfloat) g->map_h * (g->side_offset + g->side) + g->side_offset;
        }
        break;
    case LRG_TILEMAP_ORIENTATION_ORTHOGONAL:
    default:
        *w = (gfloat) g->map_w * g->cw;
        *h = (gfloat) g->map_h * g->ch;
        break;
    }
}

/* a cell's outline, clockwise from the top; returns the point count */
static guint
grid_polygon (const Grid *g,
              gint        x,
              gint        y,
              gfloat     *out)
{
    gfloat bx;
    gfloat by;
    gfloat pts[12];
    guint n;
    guint i;

    grid_bbox (g, x, y, &bx, &by);

    if (g->orientation == LRG_TILEMAP_ORIENTATION_ORTHOGONAL)
    {
        gfloat r[8] = { 0, 0, 1, 0, 1, 1, 0, 1 };

        for (i = 0; i < 8; i += 2)
        {
            pts[i] = r[i] * g->cw;
            pts[i + 1] = r[i + 1] * g->ch;
        }
        n = 4;
    }
    else if (g->orientation == LRG_TILEMAP_ORIENTATION_ISOMETRIC || g->side <= 0.0f)
    {
        /* diamond: top, right, bottom, left */
        pts[0] = g->cw * 0.5f; pts[1] = 0.0f;
        pts[2] = g->cw;        pts[3] = g->ch * 0.5f;
        pts[4] = g->cw * 0.5f; pts[5] = g->ch;
        pts[6] = 0.0f;         pts[7] = g->ch * 0.5f;
        n = 4;
    }
    else if (!g->axis_x)
    {
        /* pointy-top hexagon */
        gfloat so = g->side_offset;

        pts[0] = g->cw * 0.5f; pts[1] = 0.0f;
        pts[2] = g->cw;        pts[3] = so;
        pts[4] = g->cw;        pts[5] = so + g->side;
        pts[6] = g->cw * 0.5f; pts[7] = g->ch;
        pts[8] = 0.0f;         pts[9] = so + g->side;
        pts[10] = 0.0f;        pts[11] = so;
        n = 6;
    }
    else
    {
        /* flat-top hexagon, clockwise from the top-left corner */
        gfloat so = g->side_offset;

        pts[0] = so;           pts[1] = 0.0f;
        pts[2] = so + g->side; pts[3] = 0.0f;
        pts[4] = g->cw;        pts[5] = g->ch * 0.5f;
        pts[6] = so + g->side; pts[7] = g->ch;
        pts[8] = so;           pts[9] = g->ch;
        pts[10] = 0.0f;        pts[11] = g->ch * 0.5f;
        n = 6;
    }

    for (i = 0; i < n; i++)
    {
        out[i * 2] = bx + pts[i * 2];
        out[i * 2 + 1] = by + pts[i * 2 + 1];
    }
    return n;
}

/* point in a convex polygon (edges included) */
static gboolean
point_in_convex (const gfloat *pts,
                 guint         n,
                 gfloat        px,
                 gfloat        py)
{
    gboolean pos = FALSE;
    gboolean neg = FALSE;
    guint i;

    for (i = 0; i < n; i++)
    {
        const gfloat *a = pts + i * 2;
        const gfloat *b = pts + ((i + 1) % n) * 2;
        gfloat cross = (b[0] - a[0]) * (py - a[1]) - (b[1] - a[1]) * (px - a[0]);

        if (cross > 1e-4f)
            pos = TRUE;
        else if (cross < -1e-4f)
            neg = TRUE;
        if (pos && neg)
            return FALSE;
    }
    return TRUE;
}

/* the cell under a world point (unbounded) */
static void
grid_pick (const Grid *g,
           gfloat      wx,
           gfloat      wy,
           gint       *out_x,
           gint       *out_y)
{
    switch (g->orientation)
    {
    case LRG_TILEMAP_ORIENTATION_ISOMETRIC:
    {
        /* u, v: half-cell units from the top corner of cell (0, 0) */
        gfloat u = (wx - (gfloat) g->map_h * g->cw * 0.5f) / (g->cw * 0.5f);
        gfloat v = wy / (g->ch * 0.5f);

        *out_x = (gint) floorf ((v + u) * 0.5f);
        *out_y = (gint) floorf ((v - u) * 0.5f);
        return;
    }
    case LRG_TILEMAP_ORIENTATION_STAGGERED:
    case LRG_TILEMAP_ORIENTATION_HEXAGONAL:
    {
        /*
         * Guess from the step grid, then test the outlines of the cells
         * around the guess; the first containing one wins.  Should a
         * point fall between outlines (rounding), the nearest centre is
         * used.
         */
        gfloat col_step = g->axis_x ? g->side_offset + g->side : g->cw;
        gfloat row_step = g->axis_x ? g->ch : g->side_offset + g->side;
        gint gx = (gint) floorf (wx / MAX (col_step, 1e-3f));
        gint gy = (gint) floorf (wy / MAX (row_step, 1e-3f));
        gfloat best = G_MAXFLOAT;
        gint bx = gx;
        gint by = gy;
        gint dy;
        gint dx;

        for (dy = -2; dy <= 2; dy++)
        {
            for (dx = -2; dx <= 2; dx++)
            {
                gfloat pts[12];
                guint n = grid_polygon (g, gx + dx, gy + dy, pts);
                gfloat cx;
                gfloat cy;
                gfloat d;

                if (point_in_convex (pts, n, wx, wy))
                {
                    *out_x = gx + dx;
                    *out_y = gy + dy;
                    return;
                }
                grid_bbox (g, gx + dx, gy + dy, &cx, &cy);
                cx += g->cw * 0.5f - wx;
                cy += g->ch * 0.5f - wy;
                d = cx * cx + cy * cy;
                if (d < best)
                {
                    best = d;
                    bx = gx + dx;
                    by = gy + dy;
                }
            }
        }
        *out_x = bx;
        *out_y = by;
        return;
    }
    case LRG_TILEMAP_ORIENTATION_ORTHOGONAL:
    default:
        *out_x = (gint) floorf (wx / g->cw);
        *out_y = (gint) floorf (wy / g->ch);
        return;
    }
}

/* appends the region's cells in draw order (see lrg_tilemap_get_draw_order()) */
static void
grid_draw_order (const Grid *g,
                 gint        x0,
                 gint        y0,
                 gint        x1,
                 gint        y1,
                 GArray     *out)
{
    gint x;
    gint y;
    gint pass;

    for (y = y0; y < y1; y++)
    {
        if (grid_is_staggered (g) && g->axis_x)
        {
            /* raised (unshifted) columns first, then the lowered ones */
            for (pass = 0; pass < 2; pass++)
            {
                for (x = x0; x < x1; x++)
                {
                    gint cell[2];

                    if (grid_shifted (g, x) != (pass == 1))
                        continue;
                    cell[0] = x;
                    cell[1] = y;
                    g_array_append_vals (out, cell, 2);
                }
            }
        }
        else
        {
            for (x = x0; x < x1; x++)
            {
                gint cell[2];

                cell[0] = x;
                cell[1] = y;
                g_array_append_vals (out, cell, 2);
            }
        }
    }
}

/* ==========================================================================
 * Internal Drawing Helper
 * ========================================================================== */

/*
 * draw_cell:
 * Draws one cell value with its transform: the tile image sits with its
 * bottom-left corner on the bottom-left of the cell box plus the tileset's
 * draw offset; a mirrored tile uses a negative source width, the rotation
 * turns the image about its centre.
 */
static void
draw_cell (GrlTexture   *texture,
           LrgTileset   *tileset,
           guint         raw,
           guint64       anim_time,
           gfloat        box_x,
           gfloat        box_y,
           gfloat        cell_h,
           const GrlColor *tint)
{
    guint id = lrg_tile_get_id (raw);
    guint index;
    guint rotation;
    gfloat tw;
    gfloat th;
    gint off_x;
    gint off_y;
    GrlRectangle source;
    GrlRectangle dest;
    GrlVector2 origin;

    if (id == LRG_TILEMAP_EMPTY_TILE)
        return;
    index = id - 1;
    if (index >= lrg_tileset_get_tile_count (tileset))
        return;
    index = lrg_tileset_get_animated_tile (tileset, index, anim_time);
    if (!lrg_tileset_get_tile_rect_to (tileset, index, &source))
        return;

    tw = source.width;
    th = source.height;
    lrg_tileset_get_draw_offset (tileset, &off_x, &off_y);

    if (lrg_tile_get_mirrored (raw))
        source.width = -source.width;
    rotation = lrg_tile_get_rotation (raw);

    /* dest is centred on the image centre, which is where it rotates */
    dest.width = tw;
    dest.height = th;
    dest.x = box_x + (gfloat) off_x + tw * 0.5f;
    dest.y = box_y + cell_h - th + (gfloat) off_y + th * 0.5f;
    origin.x = tw * 0.5f;
    origin.y = th * 0.5f;

    grl_draw_texture_pro (texture, &source, &dest, &origin, (gfloat) rotation * 90.0f, tint);
}

static void
draw_layer_internal (LrgTilemap      *self,
                     LrgTilemapLayer *layer,
                     gfloat           offset_x,
                     gfloat           offset_y)
{
    LrgTilemapPrivate *priv = lrg_tilemap_get_instance_private (self);
    g_autoptr(GArray)  order = NULL;
    GrlTexture        *texture;
    const guint       *tiles;
    gsize              tiles_len;
    guint              layer_width;
    guint              layer_height;
    gint               x0;
    gint               y0;
    gint               x1;
    gint               y1;
    guint              i;
    gfloat             opacity;
    GrlColor           tint;
    GrlRectangle       view;
    Grid               g;

    if (!lrg_tilemap_layer_get_visible (layer))
        return;
    if (priv->tileset == NULL)
        return;
    texture = lrg_tileset_get_texture (priv->tileset);
    if (texture == NULL)
        return;

    layer_width = lrg_tilemap_layer_get_width (layer);
    layer_height = lrg_tilemap_layer_get_height (layer);
    tiles = lrg_tilemap_layer_get_tiles (layer, &tiles_len);
    if (tiles == NULL || lrg_tileset_get_columns (priv->tileset) == 0 ||
        tiles_len < (gsize) layer_width * layer_height)
        return;

    /* Apply opacity to tint */
    opacity = lrg_tilemap_layer_get_opacity (layer);
    tint.r = 255;
    tint.g = 255;
    tint.b = 255;
    tint.a = (guint8)(opacity * 255.0f);
    if (tint.a == 0)
        return;

    grid_init (self, &g);
    g.map_w = layer_width;
    g.map_h = layer_height;

    /*
     * Only the cells that can land in the viewport are drawn.  The visible
     * area comes from the live rlgl matrices, so it covers any camera,
     * render target or transform the caller has set up; when it cannot be
     * worked out (a perspective projection) every cell is drawn.  The
     * view's corners are picked into cell coordinates and the cell range
     * is grown by enough cells to cover tiles taller or wider than their
     * cell and staggered rows.
     */
    x0 = 0;
    y0 = 0;
    x1 = (gint) layer_width;
    y1 = (gint) layer_height;
    if (lrg_draw_cull_visible_rect_2d (&view))
    {
        gint min_x = G_MAXINT;
        gint min_y = G_MAXINT;
        gint max_x = G_MININT;
        gint max_y = G_MININT;
        gint slack;
        guint c;

        for (c = 0; c < 4; c++)
        {
            gint cx;
            gint cy;

            grid_pick (&g,
                       view.x - offset_x + ((c & 1u) != 0 ? view.width : 0.0f),
                       view.y - offset_y + ((c & 2u) != 0 ? view.height : 0.0f),
                       &cx, &cy);
            min_x = MIN (min_x, cx);
            min_y = MIN (min_y, cy);
            max_x = MAX (max_x, cx);
            max_y = MAX (max_y, cy);
        }
        slack = 2 + (gint) ceilf ((gfloat) lrg_tileset_get_tile_height (priv->tileset) / g.ch) +
                    (gint) ceilf ((gfloat) lrg_tileset_get_tile_width (priv->tileset) / g.cw);
        x0 = CLAMP (min_x - slack, 0, (gint) layer_width);
        y0 = CLAMP (min_y - slack, 0, (gint) layer_height);
        x1 = CLAMP (max_x + slack + 1, 0, (gint) layer_width);
        y1 = CLAMP (max_y + slack + 1, 0, (gint) layer_height);
    }
    if (x1 <= x0 || y1 <= y0)
        return;

    /* the orthogonal fast path walks rows directly */
    if (g.orientation == LRG_TILEMAP_ORIENTATION_ORTHOGONAL)
    {
        gint x;
        gint y;

        for (y = y0; y < y1; y++)
        {
            const guint *row = tiles + (gsize) y * layer_width;

            for (x = x0; x < x1; x++)
            {
                if (row[x] == LRG_TILEMAP_EMPTY_TILE)
                    continue;
                draw_cell (texture, priv->tileset, row[x], priv->anim_time_ms,
                           offset_x + (gfloat) x * g.cw, offset_y + (gfloat) y * g.ch, g.ch, &tint);
            }
        }
        return;
    }

    order = g_array_new (FALSE, FALSE, sizeof (gint));
    grid_draw_order (&g, x0, y0, x1, y1, order);
    for (i = 0; i + 1 < order->len; i += 2)
    {
        gint x = g_array_index (order, gint, i);
        gint y = g_array_index (order, gint, i + 1);
        guint raw = tiles[(gsize) y * layer_width + (gsize) x];
        gfloat bx;
        gfloat by;

        if (raw == LRG_TILEMAP_EMPTY_TILE)
            continue;
        grid_bbox (&g, x, y, &bx, &by);
        draw_cell (texture, priv->tileset, raw, priv->anim_time_ms,
                   offset_x + bx, offset_y + by, g.ch, &tint);
    }
}

/* ==========================================================================
 * Rendering
 * ========================================================================== */

/*
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

/*
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

/*
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

/*
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

/*
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

        tile_id = lrg_tilemap_layer_get_tile_id (layer, tile_x, tile_y);
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

/*
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

/*
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

/*
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

/*
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

/*
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

/*
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
    Grid   g;
    gfloat w;
    gfloat h;

    g_return_val_if_fail (LRG_IS_TILEMAP (self), 0);

    if (lrg_tilemap_get_tileset (self) == NULL)
        return 0;
    grid_init (self, &g);
    grid_map_size (&g, &w, &h);
    return (guint) ceilf (w);
}

/*
 * lrg_tilemap_get_pixel_height:
 * @self: an #LrgTilemap
 *
 * Gets the height of the map's bounding box in pixels (for every
 * orientation).
 *
 * Returns: The height in pixels
 */
guint
lrg_tilemap_get_pixel_height (LrgTilemap *self)
{
    Grid   g;
    gfloat w;
    gfloat h;

    g_return_val_if_fail (LRG_IS_TILEMAP (self), 0);

    if (lrg_tilemap_get_tileset (self) == NULL)
        return 0;
    grid_init (self, &g);
    grid_map_size (&g, &w, &h);
    return (guint) ceilf (h);
}

/* ==========================================================================
 * Coordinate Conversion
 * ========================================================================== */

/*
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
    gint x = 0;
    gint y = 0;

    g_return_if_fail (LRG_IS_TILEMAP (self));
    g_return_if_fail (out_tile_x != NULL);
    g_return_if_fail (out_tile_y != NULL);

    /* picks with the map's orientation; cells left of / above the map clamp to 0 */
    if (lrg_tilemap_get_tileset (self) != NULL)
        lrg_tilemap_world_to_cell (self, world_x, world_y, &x, &y);
    *out_tile_x = (guint) MAX (x, 0);
    *out_tile_y = (guint) MAX (y, 0);
}

/*
 * lrg_tilemap_tile_to_world:
 * @self: an #LrgTilemap
 * @tile_x: the tile X coordinate
 * @tile_y: the tile Y coordinate
 * @out_world_x: (out): return location for world X
 * @out_world_y: (out): return location for world Y
 *
 * Converts tile coordinates to world coordinates: the top-left corner of
 * the cell's bounding box.
 */
void
lrg_tilemap_tile_to_world (LrgTilemap *self,
                           guint       tile_x,
                           guint       tile_y,
                           gfloat     *out_world_x,
                           gfloat     *out_world_y)
{
    GrlRectangle box;

    g_return_if_fail (LRG_IS_TILEMAP (self));
    g_return_if_fail (out_world_x != NULL);
    g_return_if_fail (out_world_y != NULL);

    if (lrg_tilemap_get_tileset (self) == NULL)
    {
        *out_world_x = 0.0f;
        *out_world_y = 0.0f;
        return;
    }
    lrg_tilemap_get_cell_bounds (self, (gint) tile_x, (gint) tile_y, &box);
    *out_world_x = box.x;
    *out_world_y = box.y;
}

/* ==========================================================================
 * Orientation, cells and picking (0.9.0)
 * ========================================================================== */

GType
lrg_tilemap_orientation_get_type (void)
{
    static gsize type_id = 0;

    if (g_once_init_enter (&type_id))
    {
        static const GEnumValue values[] = {
            { LRG_TILEMAP_ORIENTATION_ORTHOGONAL, "LRG_TILEMAP_ORIENTATION_ORTHOGONAL", "orthogonal" },
            { LRG_TILEMAP_ORIENTATION_ISOMETRIC, "LRG_TILEMAP_ORIENTATION_ISOMETRIC", "isometric" },
            { LRG_TILEMAP_ORIENTATION_STAGGERED, "LRG_TILEMAP_ORIENTATION_STAGGERED", "staggered" },
            { LRG_TILEMAP_ORIENTATION_HEXAGONAL, "LRG_TILEMAP_ORIENTATION_HEXAGONAL", "hexagonal" },
            { 0, NULL, NULL }
        };
        GType id = g_enum_register_static (g_intern_static_string ("LrgTilemapOrientation"), values);

        g_once_init_leave (&type_id, id);
    }
    return (GType) type_id;
}

GType
lrg_tilemap_stagger_axis_get_type (void)
{
    static gsize type_id = 0;

    if (g_once_init_enter (&type_id))
    {
        static const GEnumValue values[] = {
            { LRG_TILEMAP_STAGGER_AXIS_X, "LRG_TILEMAP_STAGGER_AXIS_X", "x" },
            { LRG_TILEMAP_STAGGER_AXIS_Y, "LRG_TILEMAP_STAGGER_AXIS_Y", "y" },
            { 0, NULL, NULL }
        };
        GType id = g_enum_register_static (g_intern_static_string ("LrgTilemapStaggerAxis"), values);

        g_once_init_leave (&type_id, id);
    }
    return (GType) type_id;
}

GType
lrg_tilemap_stagger_index_get_type (void)
{
    static gsize type_id = 0;

    if (g_once_init_enter (&type_id))
    {
        static const GEnumValue values[] = {
            { LRG_TILEMAP_STAGGER_INDEX_ODD, "LRG_TILEMAP_STAGGER_INDEX_ODD", "odd" },
            { LRG_TILEMAP_STAGGER_INDEX_EVEN, "LRG_TILEMAP_STAGGER_INDEX_EVEN", "even" },
            { 0, NULL, NULL }
        };
        GType id = g_enum_register_static (g_intern_static_string ("LrgTilemapStaggerIndex"), values);

        g_once_init_leave (&type_id, id);
    }
    return (GType) type_id;
}

LrgTilemapOrientation
lrg_tilemap_get_orientation (LrgTilemap *self)
{
    LrgTilemapPrivate *priv;

    g_return_val_if_fail (LRG_IS_TILEMAP (self), LRG_TILEMAP_ORIENTATION_ORTHOGONAL);
    priv = lrg_tilemap_get_instance_private (self);
    return priv->orientation;
}

void
lrg_tilemap_set_orientation (LrgTilemap            *self,
                             LrgTilemapOrientation  orientation)
{
    LrgTilemapPrivate *priv;

    g_return_if_fail (LRG_IS_TILEMAP (self));
    g_return_if_fail (orientation <= LRG_TILEMAP_ORIENTATION_HEXAGONAL);

    priv = lrg_tilemap_get_instance_private (self);
    if (priv->orientation == orientation)
        return;
    priv->orientation = orientation;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_ORIENTATION]);
}

LrgTilemapStaggerAxis
lrg_tilemap_get_stagger_axis (LrgTilemap *self)
{
    LrgTilemapPrivate *priv;

    g_return_val_if_fail (LRG_IS_TILEMAP (self), LRG_TILEMAP_STAGGER_AXIS_Y);
    priv = lrg_tilemap_get_instance_private (self);
    return priv->stagger_axis;
}

void
lrg_tilemap_set_stagger_axis (LrgTilemap            *self,
                              LrgTilemapStaggerAxis  axis)
{
    LrgTilemapPrivate *priv;

    g_return_if_fail (LRG_IS_TILEMAP (self));
    g_return_if_fail (axis <= LRG_TILEMAP_STAGGER_AXIS_Y);

    priv = lrg_tilemap_get_instance_private (self);
    if (priv->stagger_axis == axis)
        return;
    priv->stagger_axis = axis;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_STAGGER_AXIS]);
}

LrgTilemapStaggerIndex
lrg_tilemap_get_stagger_index (LrgTilemap *self)
{
    LrgTilemapPrivate *priv;

    g_return_val_if_fail (LRG_IS_TILEMAP (self), LRG_TILEMAP_STAGGER_INDEX_ODD);
    priv = lrg_tilemap_get_instance_private (self);
    return priv->stagger_index;
}

void
lrg_tilemap_set_stagger_index (LrgTilemap             *self,
                               LrgTilemapStaggerIndex  index)
{
    LrgTilemapPrivate *priv;

    g_return_if_fail (LRG_IS_TILEMAP (self));
    g_return_if_fail (index <= LRG_TILEMAP_STAGGER_INDEX_EVEN);

    priv = lrg_tilemap_get_instance_private (self);
    if (priv->stagger_index == index)
        return;
    priv->stagger_index = index;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_STAGGER_INDEX]);
}

guint
lrg_tilemap_get_hex_side_length (LrgTilemap *self)
{
    LrgTilemapPrivate *priv;

    g_return_val_if_fail (LRG_IS_TILEMAP (self), 0);
    priv = lrg_tilemap_get_instance_private (self);
    return priv->hex_side_length;
}

void
lrg_tilemap_set_hex_side_length (LrgTilemap *self,
                                 guint       length)
{
    LrgTilemapPrivate *priv;

    g_return_if_fail (LRG_IS_TILEMAP (self));

    priv = lrg_tilemap_get_instance_private (self);
    if (priv->hex_side_length == length)
        return;
    priv->hex_side_length = length;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_HEX_SIDE_LENGTH]);
}

guint
lrg_tilemap_get_cell_width (LrgTilemap *self)
{
    LrgTilemapPrivate *priv;

    g_return_val_if_fail (LRG_IS_TILEMAP (self), 0);
    priv = lrg_tilemap_get_instance_private (self);
    if (priv->cell_width > 0)
        return priv->cell_width;
    return priv->tileset != NULL ? lrg_tileset_get_tile_width (priv->tileset) : 0;
}

guint
lrg_tilemap_get_cell_height (LrgTilemap *self)
{
    LrgTilemapPrivate *priv;

    g_return_val_if_fail (LRG_IS_TILEMAP (self), 0);
    priv = lrg_tilemap_get_instance_private (self);
    if (priv->cell_height > 0)
        return priv->cell_height;
    return priv->tileset != NULL ? lrg_tileset_get_tile_height (priv->tileset) : 0;
}

void
lrg_tilemap_set_cell_size (LrgTilemap *self,
                           guint       width,
                           guint       height)
{
    LrgTilemapPrivate *priv;

    g_return_if_fail (LRG_IS_TILEMAP (self));
    priv = lrg_tilemap_get_instance_private (self);
    priv->cell_width = width;
    priv->cell_height = height;
}

LrgTileNeighborLayout
lrg_tilemap_get_neighbor_layout (LrgTilemap *self)
{
    LrgTilemapPrivate *priv;
    gboolean odd;

    g_return_val_if_fail (LRG_IS_TILEMAP (self), LRG_TILE_NEIGHBOR_LAYOUT_SQUARE);

    priv = lrg_tilemap_get_instance_private (self);
    odd = priv->stagger_index == LRG_TILEMAP_STAGGER_INDEX_ODD;
    switch (priv->orientation)
    {
    case LRG_TILEMAP_ORIENTATION_STAGGERED:
        /* column-staggered diamonds are rare; rows are the supported layout */
        return odd ? LRG_TILE_NEIGHBOR_LAYOUT_STAGGERED_ODD : LRG_TILE_NEIGHBOR_LAYOUT_STAGGERED_EVEN;
    case LRG_TILEMAP_ORIENTATION_HEXAGONAL:
        if (priv->stagger_axis == LRG_TILEMAP_STAGGER_AXIS_X)
            return odd ? LRG_TILE_NEIGHBOR_LAYOUT_HEX_COLUMNS_ODD : LRG_TILE_NEIGHBOR_LAYOUT_HEX_COLUMNS_EVEN;
        return odd ? LRG_TILE_NEIGHBOR_LAYOUT_HEX_ROWS_ODD : LRG_TILE_NEIGHBOR_LAYOUT_HEX_ROWS_EVEN;
    case LRG_TILEMAP_ORIENTATION_ORTHOGONAL:
    case LRG_TILEMAP_ORIENTATION_ISOMETRIC:
    default:
        return LRG_TILE_NEIGHBOR_LAYOUT_SQUARE;
    }
}

void
lrg_tilemap_get_cell_bounds (LrgTilemap   *self,
                             gint          x,
                             gint          y,
                             GrlRectangle *out_rect)
{
    Grid g;

    g_return_if_fail (LRG_IS_TILEMAP (self));
    g_return_if_fail (out_rect != NULL);

    grid_init (self, &g);
    grid_bbox (&g, x, y, &out_rect->x, &out_rect->y);
    out_rect->width = g.cw;
    out_rect->height = g.ch;
}

void
lrg_tilemap_cell_to_world (LrgTilemap *self,
                           gint        x,
                           gint        y,
                           gfloat     *out_x,
                           gfloat     *out_y)
{
    Grid g;
    gfloat bx;
    gfloat by;

    g_return_if_fail (LRG_IS_TILEMAP (self));
    g_return_if_fail (out_x != NULL && out_y != NULL);

    grid_init (self, &g);
    grid_bbox (&g, x, y, &bx, &by);
    *out_x = bx + g.cw * 0.5f;
    *out_y = by + g.ch * 0.5f;
}

gboolean
lrg_tilemap_world_to_cell (LrgTilemap *self,
                           gfloat      world_x,
                           gfloat      world_y,
                           gint       *out_x,
                           gint       *out_y)
{
    Grid g;
    gint x;
    gint y;

    g_return_val_if_fail (LRG_IS_TILEMAP (self), FALSE);
    g_return_val_if_fail (out_x != NULL && out_y != NULL, FALSE);

    grid_init (self, &g);
    grid_pick (&g, world_x, world_y, &x, &y);
    *out_x = x;
    *out_y = y;
    return x >= 0 && y >= 0 && (guint) x < g.map_w && (guint) y < g.map_h;
}

guint
lrg_tilemap_get_cell_polygon (LrgTilemap *self,
                              gint        x,
                              gint        y,
                              gfloat     *out_coords)
{
    Grid g;

    g_return_val_if_fail (LRG_IS_TILEMAP (self), 0);
    g_return_val_if_fail (out_coords != NULL, 0);

    grid_init (self, &g);
    return grid_polygon (&g, x, y, out_coords);
}

gfloat
lrg_tilemap_get_cell_depth (LrgTilemap *self,
                            gint        x,
                            gint        y)
{
    gfloat cx;
    gfloat cy;

    g_return_val_if_fail (LRG_IS_TILEMAP (self), 0.0f);

    lrg_tilemap_cell_to_world (self, x, y, &cx, &cy);
    return cy;
}

GArray *
lrg_tilemap_get_draw_order (LrgTilemap *self,
                            gint        x,
                            gint        y,
                            guint       width,
                            guint       height)
{
    GArray *order;
    Grid g;

    g_return_val_if_fail (LRG_IS_TILEMAP (self), NULL);

    order = g_array_new (FALSE, FALSE, sizeof (gint));
    grid_init (self, &g);
    grid_draw_order (&g, x, y, x + (gint) width, y + (gint) height, order);
    return order;
}

/* ==========================================================================
 * Animated tiles and rule tiles (0.9.0)
 * ========================================================================== */

void
lrg_tilemap_update (LrgTilemap *self,
                    gfloat      delta)
{
    LrgTilemapPrivate *priv;
    gdouble ms;

    g_return_if_fail (LRG_IS_TILEMAP (self));

    if (delta <= 0.0f)
        return;
    priv = lrg_tilemap_get_instance_private (self);
    ms = (gdouble) delta * 1000.0 + priv->anim_remainder;
    priv->anim_time_ms += (guint64) ms;
    priv->anim_remainder = ms - floor (ms);
}

guint64
lrg_tilemap_get_animation_time (LrgTilemap *self)
{
    LrgTilemapPrivate *priv;

    g_return_val_if_fail (LRG_IS_TILEMAP (self), 0);
    priv = lrg_tilemap_get_instance_private (self);
    return priv->anim_time_ms;
}

void
lrg_tilemap_set_animation_time (LrgTilemap *self,
                                guint64     time_ms)
{
    LrgTilemapPrivate *priv;

    g_return_if_fail (LRG_IS_TILEMAP (self));
    priv = lrg_tilemap_get_instance_private (self);
    priv->anim_time_ms = time_ms;
    priv->anim_remainder = 0.0;
}

/*
 * with_layer_tiles:
 * Rule-tile operations work on a copy of the layer's cells; the changed
 * cells are written back one by one so ::tile-changed fires for each.
 */
typedef guint (*TilesOp) (LrgTilemap *self, guint *tiles, gsize n, guint width,
                          LrgTileNeighborLayout layout, gpointer data);

static guint
with_layer_tiles (LrgTilemap *self,
                  guint       layer_index,
                  TilesOp     op,
                  gpointer    data)
{
    LrgTilemapLayer *layer;
    g_autofree guint *copy = NULL;
    const guint *tiles;
    gsize n = 0;
    guint width;
    guint changed;
    gsize i;

    layer = lrg_tilemap_get_layer (self, layer_index);
    if (layer == NULL)
        return 0;
    tiles = lrg_tilemap_layer_get_tiles (layer, &n);
    width = lrg_tilemap_layer_get_width (layer);
    if (tiles == NULL || width == 0 || n == 0)
        return 0;

    copy = g_memdup2 (tiles, n * sizeof (guint));
    changed = op (self, copy, n, width, lrg_tilemap_get_neighbor_layout (self), data);
    if (changed == 0)
        return 0;

    for (i = 0; i < n; i++)
    {
        guint old = tiles[i];

        if (copy[i] == old)
            continue;
        lrg_tilemap_layer_set_tile (layer, (guint) (i % width), (guint) (i / width), copy[i]);
        g_signal_emit (self, signals[SIGNAL_TILE_CHANGED], 0, layer_index,
                       (guint) (i % width), (guint) (i / width), old, copy[i]);
    }
    return changed;
}

typedef struct
{
    LrgRuleTile *rule_tile;
    gint         x;
    gint         y;
} PaintArgs;

static guint
paint_op (LrgTilemap            *self,
          guint                 *tiles,
          gsize                  n,
          guint                  width,
          LrgTileNeighborLayout  layout,
          gpointer               data)
{
    PaintArgs *args = data;

    (void) self;
    return lrg_rule_tile_paint (args->rule_tile, tiles, n, width, layout, args->x, args->y);
}

static guint
erase_op (LrgTilemap            *self,
          guint                 *tiles,
          gsize                  n,
          guint                  width,
          LrgTileNeighborLayout  layout,
          gpointer               data)
{
    PaintArgs *args = data;
    LrgTileset *tileset = lrg_tilemap_get_tileset (self);
    gsize index;
    guint changed = 0;

    if (args->x < 0 || args->y < 0 || (guint) args->x >= width || (gsize) args->y >= n / width)
        return 0;
    index = (gsize) args->y * width + (gsize) args->x;
    if (tiles[index] != LRG_TILEMAP_EMPTY_TILE)
    {
        tiles[index] = LRG_TILEMAP_EMPTY_TILE;
        changed = 1;
    }
    if (tileset != NULL)
        changed += lrg_tileset_apply_rule_tiles (tileset, tiles, n, width, layout,
                                                 args->x - 2, args->y - 2, 5, 5);
    return changed;
}

static guint
refresh_op (LrgTilemap            *self,
            guint                 *tiles,
            gsize                  n,
            guint                  width,
            LrgTileNeighborLayout  layout,
            gpointer               data)
{
    LrgTileset *tileset = lrg_tilemap_get_tileset (self);

    (void) data;
    if (tileset == NULL)
        return 0;
    return lrg_tileset_apply_rule_tiles (tileset, tiles, n, width, layout, 0, 0, G_MAXUINT, G_MAXUINT);
}

guint
lrg_tilemap_paint_rule_tile (LrgTilemap  *self,
                             guint        layer_index,
                             LrgRuleTile *rule_tile,
                             gint         x,
                             gint         y)
{
    PaintArgs args;

    g_return_val_if_fail (LRG_IS_TILEMAP (self), 0);
    g_return_val_if_fail (LRG_IS_RULE_TILE (rule_tile), 0);

    args.rule_tile = rule_tile;
    args.x = x;
    args.y = y;
    return with_layer_tiles (self, layer_index, paint_op, &args);
}

guint
lrg_tilemap_erase_rule_tile (LrgTilemap *self,
                             guint       layer_index,
                             gint        x,
                             gint        y)
{
    PaintArgs args;

    g_return_val_if_fail (LRG_IS_TILEMAP (self), 0);

    args.rule_tile = NULL;
    args.x = x;
    args.y = y;
    return with_layer_tiles (self, layer_index, erase_op, &args);
}

guint
lrg_tilemap_refresh_rule_tiles (LrgTilemap *self,
                                guint       layer_index)
{
    g_return_val_if_fail (LRG_IS_TILEMAP (self), 0);
    return with_layer_tiles (self, layer_index, refresh_op, NULL);
}
