/* lrg-build-grid.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "config.h"

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_BUILDING
#include "../lrg-log.h"

#include "lrg-build-grid.h"

/* ========================================================================== */
/* LrgBuildCell boxed type                                                    */
/* ========================================================================== */

G_DEFINE_BOXED_TYPE (LrgBuildCell, lrg_build_cell,
                     lrg_build_cell_copy, lrg_build_cell_free)

LrgBuildCell *
lrg_build_cell_new (gint x,
                    gint y)
{
    LrgBuildCell *cell;

    cell = g_slice_new0 (LrgBuildCell);
    cell->x = x;
    cell->y = y;
    cell->terrain = LRG_TERRAIN_GRASS;
    cell->building = NULL;
    cell->blocked = FALSE;

    return cell;
}

LrgBuildCell *
lrg_build_cell_copy (const LrgBuildCell *cell)
{
    LrgBuildCell *copy;

    g_return_val_if_fail (cell != NULL, NULL);

    copy = g_slice_new0 (LrgBuildCell);
    copy->x = cell->x;
    copy->y = cell->y;
    copy->terrain = cell->terrain;
    copy->building = cell->building;
    copy->blocked = cell->blocked;

    return copy;
}

void
lrg_build_cell_free (LrgBuildCell *cell)
{
    if (cell == NULL)
        return;

    /* We don't own the building reference */
    g_slice_free (LrgBuildCell, cell);
}

gboolean
lrg_build_cell_is_free (const LrgBuildCell *cell)
{
    g_return_val_if_fail (cell != NULL, FALSE);

    return (cell->building == NULL && !cell->blocked);
}

/* ========================================================================== */
/* LrgBuildGrid                                                               */
/* ========================================================================== */

struct _LrgBuildGrid
{
    GObject       parent_instance;

    gint          width;
    gint          height;
    gdouble       cell_size;
    LrgBuildCell **cells;      /* 2D array: cells[y * width + x] */
    GPtrArray    *buildings;   /* All buildings on the grid */
};

enum
{
    PROP_0,
    PROP_WIDTH,
    PROP_HEIGHT,
    PROP_CELL_SIZE,
    N_PROPS
};

enum
{
    SIGNAL_BUILDING_PLACED,
    SIGNAL_BUILDING_REMOVED,
    SIGNAL_CELL_CHANGED,
    N_SIGNALS
};

static GParamSpec *properties[N_PROPS];
static guint       signals[N_SIGNALS];

G_DEFINE_TYPE (LrgBuildGrid, lrg_build_grid, G_TYPE_OBJECT)

/* Helper to get cell index */
static inline gint
cell_index (LrgBuildGrid *self,
            gint          x,
            gint          y)
{
    return y * self->width + x;
}

/* Helper to check bounds */
static inline gboolean
is_in_bounds (LrgBuildGrid *self,
              gint          x,
              gint          y)
{
    return (x >= 0 && x < self->width && y >= 0 && y < self->height);
}

/* Get effective dimensions based on rotation */
static void
get_rotated_dimensions (LrgBuildingDef *def,
                        LrgRotation     rotation,
                        gint           *out_width,
                        gint           *out_height)
{
    gint w;
    gint h;

    w = lrg_building_def_get_width (def);
    h = lrg_building_def_get_height (def);

    if (rotation == LRG_ROTATION_90 || rotation == LRG_ROTATION_270)
    {
        *out_width = h;
        *out_height = w;
    }
    else
    {
        *out_width = w;
        *out_height = h;
    }
}

static void
lrg_build_grid_dispose (GObject *object)
{
    LrgBuildGrid *self = LRG_BUILD_GRID (object);

    g_clear_pointer (&self->buildings, g_ptr_array_unref);

    G_OBJECT_CLASS (lrg_build_grid_parent_class)->dispose (object);
}

static void
lrg_build_grid_finalize (GObject *object)
{
    LrgBuildGrid *self = LRG_BUILD_GRID (object);
    gint          i;

    /* Free all cells */
    if (self->cells != NULL)
    {
        for (i = 0; i < self->width * self->height; i++)
        {
            lrg_build_cell_free (self->cells[i]);
        }
        g_free (self->cells);
    }

    G_OBJECT_CLASS (lrg_build_grid_parent_class)->finalize (object);
}

static void
lrg_build_grid_get_property (GObject    *object,
                             guint       prop_id,
                             GValue     *value,
                             GParamSpec *pspec)
{
    LrgBuildGrid *self = LRG_BUILD_GRID (object);

    switch (prop_id)
    {
    case PROP_WIDTH:
        g_value_set_int (value, self->width);
        break;
    case PROP_HEIGHT:
        g_value_set_int (value, self->height);
        break;
    case PROP_CELL_SIZE:
        g_value_set_double (value, self->cell_size);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_build_grid_set_property (GObject      *object,
                             guint         prop_id,
                             const GValue *value,
                             GParamSpec   *pspec)
{
    LrgBuildGrid *self = LRG_BUILD_GRID (object);

    switch (prop_id)
    {
    case PROP_WIDTH:
        self->width = g_value_get_int (value);
        break;
    case PROP_HEIGHT:
        self->height = g_value_get_int (value);
        break;
    case PROP_CELL_SIZE:
        self->cell_size = g_value_get_double (value);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_build_grid_constructed (GObject *object)
{
    LrgBuildGrid *self = LRG_BUILD_GRID (object);
    gint          x;
    gint          y;
    gint          count;

    G_OBJECT_CLASS (lrg_build_grid_parent_class)->constructed (object);

    /* Allocate the cell grid */
    count = self->width * self->height;
    self->cells = g_new0 (LrgBuildCell *, count);

    for (y = 0; y < self->height; y++)
    {
        for (x = 0; x < self->width; x++)
        {
            self->cells[cell_index (self, x, y)] = lrg_build_cell_new (x, y);
        }
    }

    lrg_debug (LRG_LOG_DOMAIN_BUILDING, "Created grid %dx%d with cell size %.2f",
               self->width, self->height, self->cell_size);
}

static void
lrg_build_grid_class_init (LrgBuildGridClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->dispose = lrg_build_grid_dispose;
    object_class->finalize = lrg_build_grid_finalize;
    object_class->get_property = lrg_build_grid_get_property;
    object_class->set_property = lrg_build_grid_set_property;
    object_class->constructed = lrg_build_grid_constructed;

    /**
     * LrgBuildGrid:width:
     *
     * Grid width in cells.
     *
     * Since: 1.0
     */
    properties[PROP_WIDTH] =
        g_param_spec_int ("width",
                          "Width",
                          "Grid width in cells",
                          1, G_MAXINT, 32,
                          G_PARAM_READWRITE |
                          G_PARAM_CONSTRUCT_ONLY |
                          G_PARAM_STATIC_STRINGS);

    /**
     * LrgBuildGrid:height:
     *
     * Grid height in cells.
     *
     * Since: 1.0
     */
    properties[PROP_HEIGHT] =
        g_param_spec_int ("height",
                          "Height",
                          "Grid height in cells",
                          1, G_MAXINT, 32,
                          G_PARAM_READWRITE |
                          G_PARAM_CONSTRUCT_ONLY |
                          G_PARAM_STATIC_STRINGS);

    /**
     * LrgBuildGrid:cell-size:
     *
     * Size of each cell in world units.
     *
     * Since: 1.0
     */
    properties[PROP_CELL_SIZE] =
        g_param_spec_double ("cell-size",
                             "Cell Size",
                             "Size of each cell in world units",
                             0.1, G_MAXDOUBLE, 32.0,
                             G_PARAM_READWRITE |
                             G_PARAM_CONSTRUCT_ONLY |
                             G_PARAM_STATIC_STRINGS);

    g_object_class_install_properties (object_class, N_PROPS, properties);

    /**
     * LrgBuildGrid::building-placed:
     * @grid: the #LrgBuildGrid
     * @building: the placed building
     *
     * Emitted when a building is placed on the grid.
     *
     * Since: 1.0
     */
    signals[SIGNAL_BUILDING_PLACED] =
        g_signal_new ("building-placed",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0,
                      NULL, NULL,
                      NULL,
                      G_TYPE_NONE, 1,
                      LRG_TYPE_BUILDING_INSTANCE);

    /**
     * LrgBuildGrid::building-removed:
     * @grid: the #LrgBuildGrid
     * @building: the removed building
     *
     * Emitted when a building is removed from the grid.
     *
     * Since: 1.0
     */
    signals[SIGNAL_BUILDING_REMOVED] =
        g_signal_new ("building-removed",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0,
                      NULL, NULL,
                      NULL,
                      G_TYPE_NONE, 1,
                      LRG_TYPE_BUILDING_INSTANCE);

    /**
     * LrgBuildGrid::cell-changed:
     * @grid: the #LrgBuildGrid
     * @x: cell X coordinate
     * @y: cell Y coordinate
     *
     * Emitted when a cell's state changes.
     *
     * Since: 1.0
     */
    signals[SIGNAL_CELL_CHANGED] =
        g_signal_new ("cell-changed",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0,
                      NULL, NULL,
                      NULL,
                      G_TYPE_NONE, 2,
                      G_TYPE_INT, G_TYPE_INT);
}

static void
lrg_build_grid_init (LrgBuildGrid *self)
{
    self->buildings = g_ptr_array_new_with_free_func (g_object_unref);
}

/* Public API */

LrgBuildGrid *
lrg_build_grid_new (gint    width,
                    gint    height,
                    gdouble cell_size)
{
    g_return_val_if_fail (width > 0, NULL);
    g_return_val_if_fail (height > 0, NULL);
    g_return_val_if_fail (cell_size > 0.0, NULL);

    return g_object_new (LRG_TYPE_BUILD_GRID,
                         "width", width,
                         "height", height,
                         "cell-size", cell_size,
                         NULL);
}

gint
lrg_build_grid_get_width (LrgBuildGrid *self)
{
    g_return_val_if_fail (LRG_IS_BUILD_GRID (self), 0);

    return self->width;
}

gint
lrg_build_grid_get_height (LrgBuildGrid *self)
{
    g_return_val_if_fail (LRG_IS_BUILD_GRID (self), 0);

    return self->height;
}

gdouble
lrg_build_grid_get_cell_size (LrgBuildGrid *self)
{
    g_return_val_if_fail (LRG_IS_BUILD_GRID (self), 0.0);

    return self->cell_size;
}

LrgBuildCell *
lrg_build_grid_get_cell (LrgBuildGrid *self,
                         gint          x,
                         gint          y)
{
    g_return_val_if_fail (LRG_IS_BUILD_GRID (self), NULL);

    if (!is_in_bounds (self, x, y))
        return NULL;

    return self->cells[cell_index (self, x, y)];
}

gboolean
lrg_build_grid_is_valid_cell (LrgBuildGrid *self,
                              gint          x,
                              gint          y)
{
    g_return_val_if_fail (LRG_IS_BUILD_GRID (self), FALSE);

    return is_in_bounds (self, x, y);
}

void
lrg_build_grid_set_terrain (LrgBuildGrid    *self,
                            gint             x,
                            gint             y,
                            LrgTerrainType  terrain)
{
    LrgBuildCell *cell;

    g_return_if_fail (LRG_IS_BUILD_GRID (self));

    cell = lrg_build_grid_get_cell (self, x, y);
    if (cell == NULL)
        return;

    if (cell->terrain != terrain)
    {
        cell->terrain = terrain;
        g_signal_emit (self, signals[SIGNAL_CELL_CHANGED], 0, x, y);
    }
}

LrgTerrainType
lrg_build_grid_get_terrain (LrgBuildGrid *self,
                            gint          x,
                            gint          y)
{
    LrgBuildCell *cell;

    g_return_val_if_fail (LRG_IS_BUILD_GRID (self), 0);

    cell = lrg_build_grid_get_cell (self, x, y);
    if (cell == NULL)
        return 0;

    return cell->terrain;
}

void
lrg_build_grid_fill_terrain (LrgBuildGrid    *self,
                             LrgTerrainType  terrain)
{
    gint x;
    gint y;

    g_return_if_fail (LRG_IS_BUILD_GRID (self));

    for (y = 0; y < self->height; y++)
    {
        for (x = 0; x < self->width; x++)
        {
            self->cells[cell_index (self, x, y)]->terrain = terrain;
        }
    }

    lrg_debug (LRG_LOG_DOMAIN_BUILDING, "Filled grid with terrain %d", terrain);
}

void
lrg_build_grid_set_terrain_rect (LrgBuildGrid    *self,
                                 gint             x,
                                 gint             y,
                                 gint             width,
                                 gint             height,
                                 LrgTerrainType  terrain)
{
    gint cx;
    gint cy;

    g_return_if_fail (LRG_IS_BUILD_GRID (self));

    for (cy = y; cy < y + height; cy++)
    {
        for (cx = x; cx < x + width; cx++)
        {
            lrg_build_grid_set_terrain (self, cx, cy, terrain);
        }
    }
}

void
lrg_build_grid_set_blocked (LrgBuildGrid *self,
                            gint          x,
                            gint          y,
                            gboolean      blocked)
{
    LrgBuildCell *cell;

    g_return_if_fail (LRG_IS_BUILD_GRID (self));

    cell = lrg_build_grid_get_cell (self, x, y);
    if (cell == NULL)
        return;

    if (cell->blocked != blocked)
    {
        cell->blocked = blocked;
        g_signal_emit (self, signals[SIGNAL_CELL_CHANGED], 0, x, y);
    }
}

gboolean
lrg_build_grid_is_blocked (LrgBuildGrid *self,
                           gint          x,
                           gint          y)
{
    LrgBuildCell *cell;

    g_return_val_if_fail (LRG_IS_BUILD_GRID (self), TRUE);

    cell = lrg_build_grid_get_cell (self, x, y);
    if (cell == NULL)
        return TRUE;

    return cell->blocked;
}

gboolean
lrg_build_grid_is_area_free (LrgBuildGrid *self,
                             gint          x,
                             gint          y,
                             gint          width,
                             gint          height)
{
    gint          cx;
    gint          cy;
    LrgBuildCell *cell;

    g_return_val_if_fail (LRG_IS_BUILD_GRID (self), FALSE);

    for (cy = y; cy < y + height; cy++)
    {
        for (cx = x; cx < x + width; cx++)
        {
            cell = lrg_build_grid_get_cell (self, cx, cy);
            if (cell == NULL || !lrg_build_cell_is_free (cell))
                return FALSE;
        }
    }

    return TRUE;
}

gboolean
lrg_build_grid_can_place (LrgBuildGrid   *self,
                          LrgBuildingDef *definition,
                          gint            x,
                          gint            y,
                          LrgRotation     rotation)
{
    gint             eff_width;
    gint             eff_height;
    LrgTerrainType  buildable_on;
    gint             cx;
    gint             cy;
    LrgBuildCell    *cell;

    g_return_val_if_fail (LRG_IS_BUILD_GRID (self), FALSE);
    g_return_val_if_fail (LRG_IS_BUILDING_DEF (definition), FALSE);

    get_rotated_dimensions (definition, rotation, &eff_width, &eff_height);
    buildable_on = lrg_building_def_get_buildable_on (definition);

    /* Check each cell */
    for (cy = y; cy < y + eff_height; cy++)
    {
        for (cx = x; cx < x + eff_width; cx++)
        {
            cell = lrg_build_grid_get_cell (self, cx, cy);

            /* Out of bounds */
            if (cell == NULL)
                return FALSE;

            /* Already occupied or blocked */
            if (!lrg_build_cell_is_free (cell))
                return FALSE;

            /* Terrain check */
            if ((cell->terrain & buildable_on) == 0)
                return FALSE;
        }
    }

    /* Also ask the building definition (terrain already validated above) */
    cell = lrg_build_grid_get_cell (self, x, y);
    if (!lrg_building_def_can_build (definition, x, y, cell ? cell->terrain : LRG_TERRAIN_NONE))
        return FALSE;

    return TRUE;
}

void
lrg_build_grid_world_to_cell (LrgBuildGrid *self,
                              gdouble       world_x,
                              gdouble       world_y,
                              gint         *cell_x,
                              gint         *cell_y)
{
    g_return_if_fail (LRG_IS_BUILD_GRID (self));

    if (cell_x != NULL)
        *cell_x = (gint)(world_x / self->cell_size);
    if (cell_y != NULL)
        *cell_y = (gint)(world_y / self->cell_size);
}

void
lrg_build_grid_cell_to_world (LrgBuildGrid *self,
                              gint          cell_x,
                              gint          cell_y,
                              gdouble      *world_x,
                              gdouble      *world_y)
{
    g_return_if_fail (LRG_IS_BUILD_GRID (self));

    /* Return center of cell */
    if (world_x != NULL)
        *world_x = (cell_x + 0.5) * self->cell_size;
    if (world_y != NULL)
        *world_y = (cell_y + 0.5) * self->cell_size;
}

void
lrg_build_grid_snap_to_grid (LrgBuildGrid *self,
                             gdouble       world_x,
                             gdouble       world_y,
                             gdouble      *snapped_x,
                             gdouble      *snapped_y)
{
    gint cell_x;
    gint cell_y;

    g_return_if_fail (LRG_IS_BUILD_GRID (self));

    lrg_build_grid_world_to_cell (self, world_x, world_y, &cell_x, &cell_y);
    lrg_build_grid_cell_to_world (self, cell_x, cell_y, snapped_x, snapped_y);
}

gboolean
lrg_build_grid_place_building (LrgBuildGrid        *self,
                               LrgBuildingInstance *building)
{
    LrgBuildingDef *def;
    gint            x;
    gint            y;
    LrgRotation     rotation;
    gint            eff_width;
    gint            eff_height;
    gint            cx;
    gint            cy;
    LrgBuildCell   *cell;

    g_return_val_if_fail (LRG_IS_BUILD_GRID (self), FALSE);
    g_return_val_if_fail (LRG_IS_BUILDING_INSTANCE (building), FALSE);

    def = lrg_building_instance_get_definition (building);
    x = lrg_building_instance_get_grid_x (building);
    y = lrg_building_instance_get_grid_y (building);
    rotation = lrg_building_instance_get_rotation (building);

    /* Validate placement */
    if (!lrg_build_grid_can_place (self, def, x, y, rotation))
    {
        lrg_warning (LRG_LOG_DOMAIN_BUILDING,
                     "Cannot place building at (%d, %d)", x, y);
        return FALSE;
    }

    get_rotated_dimensions (def, rotation, &eff_width, &eff_height);

    /* Mark cells as occupied */
    for (cy = y; cy < y + eff_height; cy++)
    {
        for (cx = x; cx < x + eff_width; cx++)
        {
            cell = lrg_build_grid_get_cell (self, cx, cy);
            cell->building = building;
        }
    }

    /* Add to building list */
    g_ptr_array_add (self->buildings, g_object_ref (building));

    lrg_debug (LRG_LOG_DOMAIN_BUILDING,
               "Placed building '%s' at (%d, %d)",
               lrg_building_def_get_id (def), x, y);

    g_signal_emit (self, signals[SIGNAL_BUILDING_PLACED], 0, building);

    return TRUE;
}

gboolean
lrg_build_grid_remove_building (LrgBuildGrid        *self,
                                LrgBuildingInstance *building)
{
    LrgBuildingDef *def;
    gint            x;
    gint            y;
    gint            eff_width;
    gint            eff_height;
    gint            cx;
    gint            cy;
    LrgBuildCell   *cell;
    gboolean        found;

    g_return_val_if_fail (LRG_IS_BUILD_GRID (self), FALSE);
    g_return_val_if_fail (LRG_IS_BUILDING_INSTANCE (building), FALSE);

    /* Find and remove from list */
    found = g_ptr_array_remove (self->buildings, building);
    if (!found)
    {
        lrg_warning (LRG_LOG_DOMAIN_BUILDING,
                     "Building not found on grid");
        return FALSE;
    }

    def = lrg_building_instance_get_definition (building);
    x = lrg_building_instance_get_grid_x (building);
    y = lrg_building_instance_get_grid_y (building);

    eff_width = lrg_building_instance_get_effective_width (building);
    eff_height = lrg_building_instance_get_effective_height (building);

    /* Clear cells */
    for (cy = y; cy < y + eff_height; cy++)
    {
        for (cx = x; cx < x + eff_width; cx++)
        {
            cell = lrg_build_grid_get_cell (self, cx, cy);
            if (cell != NULL)
                cell->building = NULL;
        }
    }

    lrg_debug (LRG_LOG_DOMAIN_BUILDING,
               "Removed building '%s' from (%d, %d)",
               lrg_building_def_get_id (def), x, y);

    g_signal_emit (self, signals[SIGNAL_BUILDING_REMOVED], 0, building);

    return TRUE;
}

LrgBuildingInstance *
lrg_build_grid_get_building_at (LrgBuildGrid *self,
                                gint          x,
                                gint          y)
{
    LrgBuildCell *cell;

    g_return_val_if_fail (LRG_IS_BUILD_GRID (self), NULL);

    cell = lrg_build_grid_get_cell (self, x, y);
    if (cell == NULL)
        return NULL;

    return cell->building;
}

GPtrArray *
lrg_build_grid_get_all_buildings (LrgBuildGrid *self)
{
    GPtrArray *result;
    guint      i;

    g_return_val_if_fail (LRG_IS_BUILD_GRID (self), NULL);

    result = g_ptr_array_new ();

    for (i = 0; i < self->buildings->len; i++)
    {
        g_ptr_array_add (result, g_ptr_array_index (self->buildings, i));
    }

    return result;
}

GPtrArray *
lrg_build_grid_get_buildings_in_area (LrgBuildGrid *self,
                                      gint          x,
                                      gint          y,
                                      gint          width,
                                      gint          height)
{
    GPtrArray           *result;
    gint                 cx;
    gint                 cy;
    LrgBuildCell        *cell;
    LrgBuildingInstance *building;

    g_return_val_if_fail (LRG_IS_BUILD_GRID (self), NULL);

    result = g_ptr_array_new ();

    for (cy = y; cy < y + height; cy++)
    {
        for (cx = x; cx < x + width; cx++)
        {
            cell = lrg_build_grid_get_cell (self, cx, cy);
            if (cell == NULL || cell->building == NULL)
                continue;

            building = cell->building;

            /* Avoid duplicates */
            if (!g_ptr_array_find (result, building, NULL))
            {
                g_ptr_array_add (result, building);
            }
        }
    }

    return result;
}

void
lrg_build_grid_clear (LrgBuildGrid *self)
{
    gint          i;
    gint          count;
    LrgBuildCell *cell;

    g_return_if_fail (LRG_IS_BUILD_GRID (self));

    /* Clear all cell building references */
    count = self->width * self->height;
    for (i = 0; i < count; i++)
    {
        cell = self->cells[i];
        cell->building = NULL;
    }

    /* Clear building list */
    g_ptr_array_set_size (self->buildings, 0);

    lrg_debug (LRG_LOG_DOMAIN_BUILDING, "Cleared all buildings from grid");
}
