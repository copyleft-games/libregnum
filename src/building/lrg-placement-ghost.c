/* lrg-placement-ghost.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "config.h"

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_BUILDING
#include "../lrg-log.h"

#include "lrg-placement-ghost.h"

static void lrg_placement_ghost_drawable_init (LrgDrawableInterface *iface);

struct _LrgPlacementGhost
{
    GObject              parent_instance;

    LrgPlacementSystem  *system;
    gboolean             visible;
    gboolean             show_grid;

    /* Colors */
    GrlColor             valid_color;
    GrlColor             invalid_color;
    GrlColor             demolish_color;
    GrlColor             grid_color;

    /* Custom draw function */
    LrgPlacementGhostDrawFunc draw_func;
    gpointer                  draw_func_data;
    GDestroyNotify            draw_func_destroy;
};

enum
{
    PROP_0,
    PROP_SYSTEM,
    PROP_VISIBLE,
    PROP_SHOW_GRID,
    N_PROPS
};

static GParamSpec *properties[N_PROPS];

G_DEFINE_TYPE_WITH_CODE (LrgPlacementGhost, lrg_placement_ghost, G_TYPE_OBJECT,
                         G_IMPLEMENT_INTERFACE (LRG_TYPE_DRAWABLE,
                                                lrg_placement_ghost_drawable_init))

/* Default colors */
static const GrlColor DEFAULT_VALID_COLOR = { 0, 255, 0, 128 };      /* Semi-transparent green */
static const GrlColor DEFAULT_INVALID_COLOR = { 255, 0, 0, 128 };    /* Semi-transparent red */
static const GrlColor DEFAULT_DEMOLISH_COLOR = { 255, 128, 0, 128 }; /* Semi-transparent orange */
static const GrlColor DEFAULT_GRID_COLOR = { 255, 255, 255, 64 };    /* Faint white */

/* LrgDrawable interface implementation */

static void
lrg_placement_ghost_draw_impl (LrgDrawable *drawable,
                               gfloat       delta)
{
    LrgPlacementGhost  *self = LRG_PLACEMENT_GHOST (drawable);
    LrgPlacementState   state;
    LrgBuildingDef     *def;
    LrgBuildGrid       *grid;
    gint                grid_x;
    gint                grid_y;
    gdouble             world_x;
    gdouble             world_y;
    gdouble             cell_size;
    gint                eff_width;
    gint                eff_height;
    gdouble             width;
    gdouble             height;
    LrgRotation         rotation;
    gboolean            is_valid;
    GrlColor            color;
    GrlRectangle        rect;
    gint                cx;
    gint                cy;

    (void)delta;

    if (!self->visible || self->system == NULL)
        return;

    state = lrg_placement_system_get_state (self->system);
    if (state == LRG_PLACEMENT_STATE_IDLE)
        return;

    grid = lrg_placement_system_get_grid (self->system);
    if (grid == NULL)
        return;

    lrg_placement_system_get_grid_position (self->system, &grid_x, &grid_y);
    cell_size = lrg_build_grid_get_cell_size (grid);

    if (state == LRG_PLACEMENT_STATE_PLACING)
    {
        def = lrg_placement_system_get_current_definition (self->system);
        if (def == NULL)
            return;

        rotation = lrg_placement_system_get_rotation (self->system);
        is_valid = lrg_placement_system_is_valid (self->system);

        /* Calculate effective dimensions based on rotation */
        if (rotation == LRG_ROTATION_90 || rotation == LRG_ROTATION_270)
        {
            eff_width = lrg_building_def_get_height (def);
            eff_height = lrg_building_def_get_width (def);
        }
        else
        {
            eff_width = lrg_building_def_get_width (def);
            eff_height = lrg_building_def_get_height (def);
        }

        /* World coordinates (top-left of building) */
        world_x = grid_x * cell_size;
        world_y = grid_y * cell_size;
        width = eff_width * cell_size;
        height = eff_height * cell_size;

        /* Use custom draw function if set */
        if (self->draw_func != NULL)
        {
            self->draw_func (self, def,
                             world_x, world_y,
                             width, height,
                             rotation, is_valid,
                             self->draw_func_data);
            return;
        }

        /* Choose color based on validity */
        color = is_valid ? self->valid_color : self->invalid_color;

        /* Draw the ghost rectangle */
        rect.x = (gfloat)world_x;
        rect.y = (gfloat)world_y;
        rect.width = (gfloat)width;
        rect.height = (gfloat)height;

        grl_draw_rectangle_rec (&rect, &color);

        /* Draw grid lines if enabled */
        if (self->show_grid)
        {
            /* Horizontal lines */
            for (cy = 0; cy <= eff_height; cy++)
            {
                grl_draw_line ((gint)world_x,
                               (gint)(world_y + cy * cell_size),
                               (gint)(world_x + width),
                               (gint)(world_y + cy * cell_size),
                               &self->grid_color);
            }

            /* Vertical lines */
            for (cx = 0; cx <= eff_width; cx++)
            {
                grl_draw_line ((gint)(world_x + cx * cell_size),
                               (gint)world_y,
                               (gint)(world_x + cx * cell_size),
                               (gint)(world_y + height),
                               &self->grid_color);
            }
        }
    }
    else if (state == LRG_PLACEMENT_STATE_DEMOLISHING)
    {
        LrgBuildingInstance *building;

        building = lrg_placement_system_get_building_at_cursor (self->system);
        if (building != NULL)
        {
            /* Highlight the building that would be demolished */
            gint bx;
            gint by;

            bx = lrg_building_instance_get_grid_x (building);
            by = lrg_building_instance_get_grid_y (building);
            eff_width = lrg_building_instance_get_effective_width (building);
            eff_height = lrg_building_instance_get_effective_height (building);

            rect.x = (gfloat)(bx * cell_size);
            rect.y = (gfloat)(by * cell_size);
            rect.width = (gfloat)(eff_width * cell_size);
            rect.height = (gfloat)(eff_height * cell_size);

            grl_draw_rectangle_rec (&rect, &self->demolish_color);
        }
        else
        {
            /* No building under cursor - just show cursor cell */
            rect.x = (gfloat)(grid_x * cell_size);
            rect.y = (gfloat)(grid_y * cell_size);
            rect.width = (gfloat)cell_size;
            rect.height = (gfloat)cell_size;

            grl_draw_rectangle_rec (&rect, &self->grid_color);
        }
    }
}

static void
lrg_placement_ghost_get_bounds_impl (LrgDrawable  *drawable,
                                     GrlRectangle *out_bounds)
{
    LrgPlacementGhost *self = LRG_PLACEMENT_GHOST (drawable);
    LrgBuildingDef    *def;
    LrgBuildGrid      *grid;
    gint               grid_x;
    gint               grid_y;
    gdouble            cell_size;
    gint               eff_width;
    gint               eff_height;
    LrgRotation        rotation;

    if (!self->visible || self->system == NULL)
    {
        out_bounds->x = 0;
        out_bounds->y = 0;
        out_bounds->width = 0;
        out_bounds->height = 0;
        return;
    }

    if (lrg_placement_system_get_state (self->system) != LRG_PLACEMENT_STATE_PLACING)
    {
        out_bounds->x = 0;
        out_bounds->y = 0;
        out_bounds->width = 0;
        out_bounds->height = 0;
        return;
    }

    def = lrg_placement_system_get_current_definition (self->system);
    if (def == NULL)
    {
        out_bounds->x = 0;
        out_bounds->y = 0;
        out_bounds->width = 0;
        out_bounds->height = 0;
        return;
    }

    grid = lrg_placement_system_get_grid (self->system);
    cell_size = lrg_build_grid_get_cell_size (grid);
    rotation = lrg_placement_system_get_rotation (self->system);
    lrg_placement_system_get_grid_position (self->system, &grid_x, &grid_y);

    if (rotation == LRG_ROTATION_90 || rotation == LRG_ROTATION_270)
    {
        eff_width = lrg_building_def_get_height (def);
        eff_height = lrg_building_def_get_width (def);
    }
    else
    {
        eff_width = lrg_building_def_get_width (def);
        eff_height = lrg_building_def_get_height (def);
    }

    out_bounds->x = (gfloat)(grid_x * cell_size);
    out_bounds->y = (gfloat)(grid_y * cell_size);
    out_bounds->width = (gfloat)(eff_width * cell_size);
    out_bounds->height = (gfloat)(eff_height * cell_size);
}

static void
lrg_placement_ghost_drawable_init (LrgDrawableInterface *iface)
{
    iface->draw = lrg_placement_ghost_draw_impl;
    iface->get_bounds = lrg_placement_ghost_get_bounds_impl;
}

/* GObject implementation */

static void
lrg_placement_ghost_dispose (GObject *object)
{
    LrgPlacementGhost *self = LRG_PLACEMENT_GHOST (object);

    g_clear_object (&self->system);

    if (self->draw_func_destroy != NULL && self->draw_func_data != NULL)
    {
        self->draw_func_destroy (self->draw_func_data);
        self->draw_func_data = NULL;
    }

    G_OBJECT_CLASS (lrg_placement_ghost_parent_class)->dispose (object);
}

static void
lrg_placement_ghost_get_property (GObject    *object,
                                  guint       prop_id,
                                  GValue     *value,
                                  GParamSpec *pspec)
{
    LrgPlacementGhost *self = LRG_PLACEMENT_GHOST (object);

    switch (prop_id)
    {
    case PROP_SYSTEM:
        g_value_set_object (value, self->system);
        break;
    case PROP_VISIBLE:
        g_value_set_boolean (value, self->visible);
        break;
    case PROP_SHOW_GRID:
        g_value_set_boolean (value, self->show_grid);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_placement_ghost_set_property (GObject      *object,
                                  guint         prop_id,
                                  const GValue *value,
                                  GParamSpec   *pspec)
{
    LrgPlacementGhost *self = LRG_PLACEMENT_GHOST (object);

    switch (prop_id)
    {
    case PROP_SYSTEM:
        lrg_placement_ghost_set_system (self, g_value_get_object (value));
        break;
    case PROP_VISIBLE:
        lrg_placement_ghost_set_visible (self, g_value_get_boolean (value));
        break;
    case PROP_SHOW_GRID:
        lrg_placement_ghost_set_show_grid (self, g_value_get_boolean (value));
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_placement_ghost_class_init (LrgPlacementGhostClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->dispose = lrg_placement_ghost_dispose;
    object_class->get_property = lrg_placement_ghost_get_property;
    object_class->set_property = lrg_placement_ghost_set_property;

    /**
     * LrgPlacementGhost:system:
     *
     * The placement system to visualize.
     *
     * Since: 1.0
     */
    properties[PROP_SYSTEM] =
        g_param_spec_object ("system",
                             "System",
                             "The placement system",
                             LRG_TYPE_PLACEMENT_SYSTEM,
                             G_PARAM_READWRITE |
                             G_PARAM_EXPLICIT_NOTIFY |
                             G_PARAM_STATIC_STRINGS);

    /**
     * LrgPlacementGhost:visible:
     *
     * Whether the ghost is visible.
     *
     * Since: 1.0
     */
    properties[PROP_VISIBLE] =
        g_param_spec_boolean ("visible",
                              "Visible",
                              "Whether the ghost is visible",
                              TRUE,
                              G_PARAM_READWRITE |
                              G_PARAM_EXPLICIT_NOTIFY |
                              G_PARAM_STATIC_STRINGS);

    /**
     * LrgPlacementGhost:show-grid:
     *
     * Whether to show grid lines.
     *
     * Since: 1.0
     */
    properties[PROP_SHOW_GRID] =
        g_param_spec_boolean ("show-grid",
                              "Show Grid",
                              "Whether to show grid lines",
                              TRUE,
                              G_PARAM_READWRITE |
                              G_PARAM_EXPLICIT_NOTIFY |
                              G_PARAM_STATIC_STRINGS);

    g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
lrg_placement_ghost_init (LrgPlacementGhost *self)
{
    self->visible = TRUE;
    self->show_grid = TRUE;
    self->valid_color = DEFAULT_VALID_COLOR;
    self->invalid_color = DEFAULT_INVALID_COLOR;
    self->demolish_color = DEFAULT_DEMOLISH_COLOR;
    self->grid_color = DEFAULT_GRID_COLOR;
}

/* Public API */

LrgPlacementGhost *
lrg_placement_ghost_new (LrgPlacementSystem *system)
{
    g_return_val_if_fail (system == NULL || LRG_IS_PLACEMENT_SYSTEM (system), NULL);

    return g_object_new (LRG_TYPE_PLACEMENT_GHOST,
                         "system", system,
                         NULL);
}

LrgPlacementSystem *
lrg_placement_ghost_get_system (LrgPlacementGhost *self)
{
    g_return_val_if_fail (LRG_IS_PLACEMENT_GHOST (self), NULL);

    return self->system;
}

void
lrg_placement_ghost_set_system (LrgPlacementGhost  *self,
                                LrgPlacementSystem *system)
{
    g_return_if_fail (LRG_IS_PLACEMENT_GHOST (self));
    g_return_if_fail (system == NULL || LRG_IS_PLACEMENT_SYSTEM (system));

    if (g_set_object (&self->system, system))
    {
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SYSTEM]);
    }
}

void
lrg_placement_ghost_set_valid_color (LrgPlacementGhost *self,
                                     GrlColor          *color)
{
    g_return_if_fail (LRG_IS_PLACEMENT_GHOST (self));
    g_return_if_fail (color != NULL);

    self->valid_color = *color;
}

void
lrg_placement_ghost_get_valid_color (LrgPlacementGhost *self,
                                     GrlColor          *out_color)
{
    g_return_if_fail (LRG_IS_PLACEMENT_GHOST (self));
    g_return_if_fail (out_color != NULL);

    *out_color = self->valid_color;
}

void
lrg_placement_ghost_set_invalid_color (LrgPlacementGhost *self,
                                       GrlColor          *color)
{
    g_return_if_fail (LRG_IS_PLACEMENT_GHOST (self));
    g_return_if_fail (color != NULL);

    self->invalid_color = *color;
}

void
lrg_placement_ghost_get_invalid_color (LrgPlacementGhost *self,
                                       GrlColor          *out_color)
{
    g_return_if_fail (LRG_IS_PLACEMENT_GHOST (self));
    g_return_if_fail (out_color != NULL);

    *out_color = self->invalid_color;
}

void
lrg_placement_ghost_set_demolish_color (LrgPlacementGhost *self,
                                        GrlColor          *color)
{
    g_return_if_fail (LRG_IS_PLACEMENT_GHOST (self));
    g_return_if_fail (color != NULL);

    self->demolish_color = *color;
}

void
lrg_placement_ghost_get_demolish_color (LrgPlacementGhost *self,
                                        GrlColor          *out_color)
{
    g_return_if_fail (LRG_IS_PLACEMENT_GHOST (self));
    g_return_if_fail (out_color != NULL);

    *out_color = self->demolish_color;
}

gboolean
lrg_placement_ghost_get_visible (LrgPlacementGhost *self)
{
    g_return_val_if_fail (LRG_IS_PLACEMENT_GHOST (self), FALSE);

    return self->visible;
}

void
lrg_placement_ghost_set_visible (LrgPlacementGhost *self,
                                 gboolean           visible)
{
    g_return_if_fail (LRG_IS_PLACEMENT_GHOST (self));

    visible = !!visible;
    if (self->visible != visible)
    {
        self->visible = visible;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_VISIBLE]);
    }
}

gboolean
lrg_placement_ghost_get_show_grid (LrgPlacementGhost *self)
{
    g_return_val_if_fail (LRG_IS_PLACEMENT_GHOST (self), FALSE);

    return self->show_grid;
}

void
lrg_placement_ghost_set_show_grid (LrgPlacementGhost *self,
                                   gboolean           show_grid)
{
    g_return_if_fail (LRG_IS_PLACEMENT_GHOST (self));

    show_grid = !!show_grid;
    if (self->show_grid != show_grid)
    {
        self->show_grid = show_grid;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SHOW_GRID]);
    }
}

void
lrg_placement_ghost_set_grid_color (LrgPlacementGhost *self,
                                    GrlColor          *color)
{
    g_return_if_fail (LRG_IS_PLACEMENT_GHOST (self));
    g_return_if_fail (color != NULL);

    self->grid_color = *color;
}

void
lrg_placement_ghost_set_draw_func (LrgPlacementGhost         *self,
                                   LrgPlacementGhostDrawFunc  func,
                                   gpointer                   user_data,
                                   GDestroyNotify             destroy)
{
    g_return_if_fail (LRG_IS_PLACEMENT_GHOST (self));

    /* Clean up old callback */
    if (self->draw_func_destroy != NULL && self->draw_func_data != NULL)
    {
        self->draw_func_destroy (self->draw_func_data);
    }

    self->draw_func = func;
    self->draw_func_data = user_data;
    self->draw_func_destroy = destroy;
}
