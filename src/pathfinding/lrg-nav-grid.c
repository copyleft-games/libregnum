/* lrg-nav-grid.c
 *
 * Copyright 2024 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Navigation grid implementation.
 */

#include "lrg-nav-grid.h"

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_PATHFIND
#include "lrg-log.h"

typedef struct
{
    guint        width;
    guint        height;
    LrgNavCell **cells;          /* 2D array stored as 1D */
    gboolean     allow_diagonal;
    gboolean     cut_corners;
} LrgNavGridPrivate;

#pragma GCC visibility push(default)
G_DEFINE_TYPE_WITH_PRIVATE (LrgNavGrid, lrg_nav_grid, G_TYPE_OBJECT)
#pragma GCC visibility pop

enum
{
    PROP_0,
    PROP_WIDTH,
    PROP_HEIGHT,
    PROP_ALLOW_DIAGONAL,
    PROP_CUT_CORNERS,
    N_PROPS
};

static GParamSpec *properties[N_PROPS];

/* Direction offsets for neighbors: N, E, S, W, NE, SE, SW, NW */
static const gint DIR_X[] = { 0, 1, 0, -1, 1, 1, -1, -1 };
static const gint DIR_Y[] = { -1, 0, 1, 0, -1, 1, 1, -1 };

/*
 * get_cell_index:
 *
 * Converts 2D coordinates to 1D array index.
 */
static inline guint
get_cell_index (LrgNavGridPrivate *priv,
                gint               x,
                gint               y)
{
    return (guint)y * priv->width + (guint)x;
}

/*
 * default_get_cell_cost:
 *
 * Default implementation of get_cell_cost virtual method.
 */
static gfloat
default_get_cell_cost (LrgNavGrid *self,
                       gint        x,
                       gint        y)
{
    LrgNavGridPrivate *priv = lrg_nav_grid_get_instance_private (self);
    guint idx;

    if (x < 0 || (guint)x >= priv->width || y < 0 || (guint)y >= priv->height)
        return G_MAXFLOAT;

    idx = get_cell_index (priv, x, y);
    return lrg_nav_cell_get_cost (priv->cells[idx]);
}

/*
 * default_is_cell_walkable:
 *
 * Default implementation of is_cell_walkable virtual method.
 */
static gboolean
default_is_cell_walkable (LrgNavGrid *self,
                          gint        x,
                          gint        y)
{
    LrgNavGridPrivate *priv = lrg_nav_grid_get_instance_private (self);
    guint idx;

    if (x < 0 || (guint)x >= priv->width || y < 0 || (guint)y >= priv->height)
        return FALSE;

    idx = get_cell_index (priv, x, y);
    return lrg_nav_cell_is_walkable (priv->cells[idx]);
}

/*
 * default_get_neighbors:
 *
 * Default implementation of get_neighbors virtual method.
 * Returns cardinal directions and optionally diagonals.
 */
static GList *
default_get_neighbors (LrgNavGrid *self,
                       gint        x,
                       gint        y)
{
    LrgNavGridPrivate *priv = lrg_nav_grid_get_instance_private (self);
    GList *neighbors = NULL;
    gint num_dirs;
    gint i;

    num_dirs = priv->allow_diagonal ? 8 : 4;

    for (i = 0; i < num_dirs; i++)
    {
        gint nx = x + DIR_X[i];
        gint ny = y + DIR_Y[i];

        if (!lrg_nav_grid_is_walkable (self, nx, ny))
            continue;

        /* Check corner cutting for diagonals */
        if (i >= 4 && !priv->cut_corners)
        {
            /* For diagonal movement, both adjacent cells must be walkable */
            gboolean adj1_walkable = FALSE;
            gboolean adj2_walkable = FALSE;

            switch (i)
            {
            case 4: /* NE */
                adj1_walkable = lrg_nav_grid_is_walkable (self, x, y - 1);
                adj2_walkable = lrg_nav_grid_is_walkable (self, x + 1, y);
                break;
            case 5: /* SE */
                adj1_walkable = lrg_nav_grid_is_walkable (self, x + 1, y);
                adj2_walkable = lrg_nav_grid_is_walkable (self, x, y + 1);
                break;
            case 6: /* SW */
                adj1_walkable = lrg_nav_grid_is_walkable (self, x, y + 1);
                adj2_walkable = lrg_nav_grid_is_walkable (self, x - 1, y);
                break;
            case 7: /* NW */
                adj1_walkable = lrg_nav_grid_is_walkable (self, x - 1, y);
                adj2_walkable = lrg_nav_grid_is_walkable (self, x, y - 1);
                break;
            }

            if (!adj1_walkable || !adj2_walkable)
                continue;
        }

        neighbors = g_list_prepend (neighbors,
                                    lrg_nav_cell_copy (priv->cells[get_cell_index (priv, nx, ny)]));
    }

    return neighbors;
}

static void
lrg_nav_grid_get_property (GObject    *object,
                           guint       prop_id,
                           GValue     *value,
                           GParamSpec *pspec)
{
    LrgNavGrid *self = LRG_NAV_GRID (object);
    LrgNavGridPrivate *priv = lrg_nav_grid_get_instance_private (self);

    switch (prop_id)
    {
    case PROP_WIDTH:
        g_value_set_uint (value, priv->width);
        break;
    case PROP_HEIGHT:
        g_value_set_uint (value, priv->height);
        break;
    case PROP_ALLOW_DIAGONAL:
        g_value_set_boolean (value, priv->allow_diagonal);
        break;
    case PROP_CUT_CORNERS:
        g_value_set_boolean (value, priv->cut_corners);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_nav_grid_set_property (GObject      *object,
                           guint         prop_id,
                           const GValue *value,
                           GParamSpec   *pspec)
{
    LrgNavGrid *self = LRG_NAV_GRID (object);
    LrgNavGridPrivate *priv = lrg_nav_grid_get_instance_private (self);

    switch (prop_id)
    {
    case PROP_WIDTH:
        priv->width = g_value_get_uint (value);
        break;
    case PROP_HEIGHT:
        priv->height = g_value_get_uint (value);
        break;
    case PROP_ALLOW_DIAGONAL:
        priv->allow_diagonal = g_value_get_boolean (value);
        break;
    case PROP_CUT_CORNERS:
        priv->cut_corners = g_value_get_boolean (value);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_nav_grid_constructed (GObject *object)
{
    LrgNavGrid *self = LRG_NAV_GRID (object);
    LrgNavGridPrivate *priv = lrg_nav_grid_get_instance_private (self);
    guint total_cells;
    guint i;

    G_OBJECT_CLASS (lrg_nav_grid_parent_class)->constructed (object);

    /* Allocate and initialize cells */
    total_cells = priv->width * priv->height;
    priv->cells = g_new0 (LrgNavCell *, total_cells);

    for (i = 0; i < total_cells; i++)
    {
        gint x = (gint)(i % priv->width);
        gint y = (gint)(i / priv->width);
        priv->cells[i] = lrg_nav_cell_new (x, y, 1.0f, LRG_NAV_CELL_NONE);
    }

    lrg_log_debug ("Created navigation grid %ux%u", priv->width, priv->height);
}

static void
lrg_nav_grid_finalize (GObject *object)
{
    LrgNavGrid *self = LRG_NAV_GRID (object);
    LrgNavGridPrivate *priv = lrg_nav_grid_get_instance_private (self);
    guint total_cells;
    guint i;

    total_cells = priv->width * priv->height;
    for (i = 0; i < total_cells; i++)
    {
        lrg_nav_cell_free (priv->cells[i]);
    }
    g_free (priv->cells);

    G_OBJECT_CLASS (lrg_nav_grid_parent_class)->finalize (object);
}

static void
lrg_nav_grid_class_init (LrgNavGridClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->get_property = lrg_nav_grid_get_property;
    object_class->set_property = lrg_nav_grid_set_property;
    object_class->constructed = lrg_nav_grid_constructed;
    object_class->finalize = lrg_nav_grid_finalize;

    /* Virtual method defaults */
    klass->get_cell_cost = default_get_cell_cost;
    klass->is_cell_walkable = default_is_cell_walkable;
    klass->get_neighbors = default_get_neighbors;

    /**
     * LrgNavGrid:width:
     *
     * The width of the grid in cells.
     */
    properties[PROP_WIDTH] =
        g_param_spec_uint ("width",
                           "Width",
                           "Grid width in cells",
                           1, G_MAXUINT, 10,
                           G_PARAM_READWRITE |
                           G_PARAM_CONSTRUCT_ONLY |
                           G_PARAM_STATIC_STRINGS);

    /**
     * LrgNavGrid:height:
     *
     * The height of the grid in cells.
     */
    properties[PROP_HEIGHT] =
        g_param_spec_uint ("height",
                           "Height",
                           "Grid height in cells",
                           1, G_MAXUINT, 10,
                           G_PARAM_READWRITE |
                           G_PARAM_CONSTRUCT_ONLY |
                           G_PARAM_STATIC_STRINGS);

    /**
     * LrgNavGrid:allow-diagonal:
     *
     * Whether diagonal movement is allowed.
     */
    properties[PROP_ALLOW_DIAGONAL] =
        g_param_spec_boolean ("allow-diagonal",
                              "Allow Diagonal",
                              "Whether diagonal movement is allowed",
                              TRUE,
                              G_PARAM_READWRITE |
                              G_PARAM_STATIC_STRINGS);

    /**
     * LrgNavGrid:cut-corners:
     *
     * Whether corner cutting is allowed for diagonal movement.
     */
    properties[PROP_CUT_CORNERS] =
        g_param_spec_boolean ("cut-corners",
                              "Cut Corners",
                              "Whether corner cutting is allowed",
                              FALSE,
                              G_PARAM_READWRITE |
                              G_PARAM_STATIC_STRINGS);

    g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
lrg_nav_grid_init (LrgNavGrid *self)
{
    LrgNavGridPrivate *priv = lrg_nav_grid_get_instance_private (self);

    priv->width = 0;
    priv->height = 0;
    priv->cells = NULL;
    priv->allow_diagonal = TRUE;
    priv->cut_corners = FALSE;
}

/**
 * lrg_nav_grid_new:
 * @width: Grid width in cells
 * @height: Grid height in cells
 *
 * Creates a new navigation grid with all cells walkable.
 *
 * Returns: (transfer full): A new #LrgNavGrid
 */
LrgNavGrid *
lrg_nav_grid_new (guint width,
                  guint height)
{
    return g_object_new (LRG_TYPE_NAV_GRID,
                         "width", width,
                         "height", height,
                         NULL);
}

/**
 * lrg_nav_grid_get_width:
 * @self: an #LrgNavGrid
 *
 * Gets the grid width.
 *
 * Returns: Grid width in cells
 */
guint
lrg_nav_grid_get_width (LrgNavGrid *self)
{
    LrgNavGridPrivate *priv;

    g_return_val_if_fail (LRG_IS_NAV_GRID (self), 0);

    priv = lrg_nav_grid_get_instance_private (self);
    return priv->width;
}

/**
 * lrg_nav_grid_get_height:
 * @self: an #LrgNavGrid
 *
 * Gets the grid height.
 *
 * Returns: Grid height in cells
 */
guint
lrg_nav_grid_get_height (LrgNavGrid *self)
{
    LrgNavGridPrivate *priv;

    g_return_val_if_fail (LRG_IS_NAV_GRID (self), 0);

    priv = lrg_nav_grid_get_instance_private (self);
    return priv->height;
}

/**
 * lrg_nav_grid_is_valid:
 * @self: an #LrgNavGrid
 * @x: X coordinate
 * @y: Y coordinate
 *
 * Checks if coordinates are within grid bounds.
 *
 * Returns: %TRUE if coordinates are valid
 */
gboolean
lrg_nav_grid_is_valid (LrgNavGrid *self,
                       gint        x,
                       gint        y)
{
    LrgNavGridPrivate *priv;

    g_return_val_if_fail (LRG_IS_NAV_GRID (self), FALSE);

    priv = lrg_nav_grid_get_instance_private (self);
    return (x >= 0 && (guint)x < priv->width &&
            y >= 0 && (guint)y < priv->height);
}

/**
 * lrg_nav_grid_get_cell:
 * @self: an #LrgNavGrid
 * @x: X coordinate
 * @y: Y coordinate
 *
 * Gets the navigation cell at the specified position.
 *
 * Returns: (transfer none) (nullable): The cell or %NULL if invalid
 */
LrgNavCell *
lrg_nav_grid_get_cell (LrgNavGrid *self,
                       gint        x,
                       gint        y)
{
    LrgNavGridPrivate *priv;

    g_return_val_if_fail (LRG_IS_NAV_GRID (self), NULL);

    if (!lrg_nav_grid_is_valid (self, x, y))
        return NULL;

    priv = lrg_nav_grid_get_instance_private (self);
    return priv->cells[get_cell_index (priv, x, y)];
}

/**
 * lrg_nav_grid_set_cell_cost:
 * @self: an #LrgNavGrid
 * @x: X coordinate
 * @y: Y coordinate
 * @cost: Movement cost
 *
 * Sets the movement cost for a cell.
 */
void
lrg_nav_grid_set_cell_cost (LrgNavGrid *self,
                            gint        x,
                            gint        y,
                            gfloat      cost)
{
    LrgNavCell *cell;

    g_return_if_fail (LRG_IS_NAV_GRID (self));

    cell = lrg_nav_grid_get_cell (self, x, y);
    if (cell != NULL)
        lrg_nav_cell_set_cost (cell, cost);
}

/**
 * lrg_nav_grid_get_cell_cost:
 * @self: an #LrgNavGrid
 * @x: X coordinate
 * @y: Y coordinate
 *
 * Gets the movement cost for a cell.
 *
 * Returns: Movement cost, or G_MAXFLOAT if invalid
 */
gfloat
lrg_nav_grid_get_cell_cost (LrgNavGrid *self,
                            gint        x,
                            gint        y)
{
    LrgNavGridClass *klass;

    g_return_val_if_fail (LRG_IS_NAV_GRID (self), G_MAXFLOAT);

    klass = LRG_NAV_GRID_GET_CLASS (self);
    return klass->get_cell_cost (self, x, y);
}

/**
 * lrg_nav_grid_set_cell_flags:
 * @self: an #LrgNavGrid
 * @x: X coordinate
 * @y: Y coordinate
 * @flags: Navigation flags
 *
 * Sets the navigation flags for a cell.
 */
void
lrg_nav_grid_set_cell_flags (LrgNavGrid      *self,
                             gint             x,
                             gint             y,
                             LrgNavCellFlags  flags)
{
    LrgNavCell *cell;

    g_return_if_fail (LRG_IS_NAV_GRID (self));

    cell = lrg_nav_grid_get_cell (self, x, y);
    if (cell != NULL)
        lrg_nav_cell_set_flags (cell, flags);
}

/**
 * lrg_nav_grid_get_cell_flags:
 * @self: an #LrgNavGrid
 * @x: X coordinate
 * @y: Y coordinate
 *
 * Gets the navigation flags for a cell.
 *
 * Returns: Navigation flags
 */
LrgNavCellFlags
lrg_nav_grid_get_cell_flags (LrgNavGrid *self,
                             gint        x,
                             gint        y)
{
    LrgNavCell *cell;

    g_return_val_if_fail (LRG_IS_NAV_GRID (self), LRG_NAV_CELL_NONE);

    cell = lrg_nav_grid_get_cell (self, x, y);
    if (cell == NULL)
        return LRG_NAV_CELL_NONE;

    return lrg_nav_cell_get_flags (cell);
}

/**
 * lrg_nav_grid_set_blocked:
 * @self: an #LrgNavGrid
 * @x: X coordinate
 * @y: Y coordinate
 * @blocked: Whether the cell is blocked
 *
 * Sets whether a cell is blocked (not walkable).
 */
void
lrg_nav_grid_set_blocked (LrgNavGrid *self,
                          gint        x,
                          gint        y,
                          gboolean    blocked)
{
    LrgNavCell *cell;
    LrgNavCellFlags flags;

    g_return_if_fail (LRG_IS_NAV_GRID (self));

    cell = lrg_nav_grid_get_cell (self, x, y);
    if (cell == NULL)
        return;

    flags = lrg_nav_cell_get_flags (cell);
    if (blocked)
        flags |= LRG_NAV_CELL_BLOCKED;
    else
        flags &= ~LRG_NAV_CELL_BLOCKED;

    lrg_nav_cell_set_flags (cell, flags);
}

/**
 * lrg_nav_grid_is_walkable:
 * @self: an #LrgNavGrid
 * @x: X coordinate
 * @y: Y coordinate
 *
 * Checks if a cell is walkable.
 *
 * Returns: %TRUE if walkable
 */
gboolean
lrg_nav_grid_is_walkable (LrgNavGrid *self,
                          gint        x,
                          gint        y)
{
    LrgNavGridClass *klass;

    g_return_val_if_fail (LRG_IS_NAV_GRID (self), FALSE);

    klass = LRG_NAV_GRID_GET_CLASS (self);
    return klass->is_cell_walkable (self, x, y);
}

/**
 * lrg_nav_grid_get_allow_diagonal:
 * @self: an #LrgNavGrid
 *
 * Gets whether diagonal movement is allowed.
 *
 * Returns: %TRUE if diagonal movement is allowed
 */
gboolean
lrg_nav_grid_get_allow_diagonal (LrgNavGrid *self)
{
    LrgNavGridPrivate *priv;

    g_return_val_if_fail (LRG_IS_NAV_GRID (self), TRUE);

    priv = lrg_nav_grid_get_instance_private (self);
    return priv->allow_diagonal;
}

/**
 * lrg_nav_grid_set_allow_diagonal:
 * @self: an #LrgNavGrid
 * @allow: Whether to allow diagonal movement
 *
 * Sets whether diagonal movement is allowed.
 */
void
lrg_nav_grid_set_allow_diagonal (LrgNavGrid *self,
                                 gboolean    allow)
{
    LrgNavGridPrivate *priv;

    g_return_if_fail (LRG_IS_NAV_GRID (self));

    priv = lrg_nav_grid_get_instance_private (self);
    priv->allow_diagonal = allow;
}

/**
 * lrg_nav_grid_get_cut_corners:
 * @self: an #LrgNavGrid
 *
 * Gets whether corner cutting is allowed for diagonal movement.
 *
 * Returns: %TRUE if corner cutting is allowed
 */
gboolean
lrg_nav_grid_get_cut_corners (LrgNavGrid *self)
{
    LrgNavGridPrivate *priv;

    g_return_val_if_fail (LRG_IS_NAV_GRID (self), FALSE);

    priv = lrg_nav_grid_get_instance_private (self);
    return priv->cut_corners;
}

/**
 * lrg_nav_grid_set_cut_corners:
 * @self: an #LrgNavGrid
 * @allow: Whether to allow corner cutting
 *
 * Sets whether corner cutting is allowed.
 */
void
lrg_nav_grid_set_cut_corners (LrgNavGrid *self,
                              gboolean    allow)
{
    LrgNavGridPrivate *priv;

    g_return_if_fail (LRG_IS_NAV_GRID (self));

    priv = lrg_nav_grid_get_instance_private (self);
    priv->cut_corners = allow;
}

/**
 * lrg_nav_grid_get_neighbors:
 * @self: an #LrgNavGrid
 * @x: X coordinate
 * @y: Y coordinate
 *
 * Gets all walkable neighbors of a cell.
 *
 * Returns: (transfer full) (element-type LrgNavCell): List of neighbor cells
 */
GList *
lrg_nav_grid_get_neighbors (LrgNavGrid *self,
                            gint        x,
                            gint        y)
{
    LrgNavGridClass *klass;

    g_return_val_if_fail (LRG_IS_NAV_GRID (self), NULL);

    klass = LRG_NAV_GRID_GET_CLASS (self);
    return klass->get_neighbors (self, x, y);
}

/**
 * lrg_nav_grid_clear:
 * @self: an #LrgNavGrid
 *
 * Resets all cells to default (walkable, cost 1.0).
 */
void
lrg_nav_grid_clear (LrgNavGrid *self)
{
    LrgNavGridPrivate *priv;
    guint total_cells;
    guint i;

    g_return_if_fail (LRG_IS_NAV_GRID (self));

    priv = lrg_nav_grid_get_instance_private (self);
    total_cells = priv->width * priv->height;

    for (i = 0; i < total_cells; i++)
    {
        lrg_nav_cell_set_cost (priv->cells[i], 1.0f);
        lrg_nav_cell_set_flags (priv->cells[i], LRG_NAV_CELL_NONE);
    }

    lrg_log_debug ("Cleared navigation grid");
}

/**
 * lrg_nav_grid_fill_rect:
 * @self: an #LrgNavGrid
 * @x: Starting X coordinate
 * @y: Starting Y coordinate
 * @width: Rectangle width
 * @height: Rectangle height
 * @flags: Flags to set
 * @cost: Cost to set
 *
 * Fills a rectangular area with specified flags and cost.
 */
void
lrg_nav_grid_fill_rect (LrgNavGrid      *self,
                        gint             x,
                        gint             y,
                        guint            width,
                        guint            height,
                        LrgNavCellFlags  flags,
                        gfloat           cost)
{
    gint cx;
    gint cy;

    g_return_if_fail (LRG_IS_NAV_GRID (self));

    for (cy = y; cy < y + (gint)height; cy++)
    {
        for (cx = x; cx < x + (gint)width; cx++)
        {
            LrgNavCell *cell = lrg_nav_grid_get_cell (self, cx, cy);
            if (cell != NULL)
            {
                lrg_nav_cell_set_flags (cell, flags);
                lrg_nav_cell_set_cost (cell, cost);
            }
        }
    }
}
