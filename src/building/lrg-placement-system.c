/* lrg-placement-system.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "config.h"

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_BUILDING
#include "../lrg-log.h"

#include "lrg-placement-system.h"

struct _LrgPlacementSystem
{
    GObject             parent_instance;

    LrgBuildGrid       *grid;
    LrgPlacementState   state;

    /* Current placement info */
    LrgBuildingDef     *current_def;
    gint                grid_x;
    gint                grid_y;
    LrgRotation         rotation;
    gboolean            is_valid;

    /* Resource check callback */
    LrgPlacementResourceCheck resource_check;
    gpointer                  resource_check_data;
    GDestroyNotify            resource_check_destroy;
};

enum
{
    PROP_0,
    PROP_GRID,
    PROP_STATE,
    PROP_IS_VALID,
    N_PROPS
};

enum
{
    SIGNAL_PLACEMENT_STARTED,
    SIGNAL_PLACEMENT_CANCELLED,
    SIGNAL_PLACEMENT_CONFIRMED,
    SIGNAL_BUILDING_DEMOLISHED,
    SIGNAL_VALIDITY_CHANGED,
    N_SIGNALS
};

static GParamSpec *properties[N_PROPS];
static guint       signals[N_SIGNALS];

G_DEFINE_TYPE (LrgPlacementSystem, lrg_placement_system, G_TYPE_OBJECT)

static void
update_validity (LrgPlacementSystem *self)
{
    gboolean new_valid;

    if (self->state != LRG_PLACEMENT_STATE_PLACING || self->current_def == NULL)
    {
        new_valid = FALSE;
    }
    else
    {
        /* Check grid placement */
        new_valid = lrg_build_grid_can_place (self->grid,
                                              self->current_def,
                                              self->grid_x,
                                              self->grid_y,
                                              self->rotation);

        /* Check resources if we have a callback */
        if (new_valid && self->resource_check != NULL)
        {
            new_valid = self->resource_check (self->current_def, 1,
                                              self->resource_check_data);
        }
    }

    if (self->is_valid != new_valid)
    {
        self->is_valid = new_valid;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_IS_VALID]);
        g_signal_emit (self, signals[SIGNAL_VALIDITY_CHANGED], 0, new_valid);
    }
}

static void
lrg_placement_system_dispose (GObject *object)
{
    LrgPlacementSystem *self = LRG_PLACEMENT_SYSTEM (object);

    g_clear_object (&self->grid);
    g_clear_object (&self->current_def);

    if (self->resource_check_destroy != NULL && self->resource_check_data != NULL)
    {
        self->resource_check_destroy (self->resource_check_data);
        self->resource_check_data = NULL;
    }

    G_OBJECT_CLASS (lrg_placement_system_parent_class)->dispose (object);
}

static void
lrg_placement_system_get_property (GObject    *object,
                                   guint       prop_id,
                                   GValue     *value,
                                   GParamSpec *pspec)
{
    LrgPlacementSystem *self = LRG_PLACEMENT_SYSTEM (object);

    switch (prop_id)
    {
    case PROP_GRID:
        g_value_set_object (value, self->grid);
        break;
    case PROP_STATE:
        g_value_set_int (value, self->state);
        break;
    case PROP_IS_VALID:
        g_value_set_boolean (value, self->is_valid);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_placement_system_set_property (GObject      *object,
                                   guint         prop_id,
                                   const GValue *value,
                                   GParamSpec   *pspec)
{
    LrgPlacementSystem *self = LRG_PLACEMENT_SYSTEM (object);

    switch (prop_id)
    {
    case PROP_GRID:
        lrg_placement_system_set_grid (self, g_value_get_object (value));
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_placement_system_class_init (LrgPlacementSystemClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->dispose = lrg_placement_system_dispose;
    object_class->get_property = lrg_placement_system_get_property;
    object_class->set_property = lrg_placement_system_set_property;

    /**
     * LrgPlacementSystem:grid:
     *
     * The build grid to place on.
     *
     * Since: 1.0
     */
    properties[PROP_GRID] =
        g_param_spec_object ("grid",
                             "Grid",
                             "The build grid",
                             LRG_TYPE_BUILD_GRID,
                             G_PARAM_READWRITE |
                             G_PARAM_EXPLICIT_NOTIFY |
                             G_PARAM_STATIC_STRINGS);

    /**
     * LrgPlacementSystem:state:
     *
     * Current placement state.
     *
     * Since: 1.0
     */
    properties[PROP_STATE] =
        g_param_spec_int ("state",
                          "State",
                          "Current placement state",
                          LRG_PLACEMENT_STATE_IDLE,
                          LRG_PLACEMENT_STATE_DEMOLISHING,
                          LRG_PLACEMENT_STATE_IDLE,
                          G_PARAM_READABLE |
                          G_PARAM_STATIC_STRINGS);

    /**
     * LrgPlacementSystem:is-valid:
     *
     * Whether current placement is valid.
     *
     * Since: 1.0
     */
    properties[PROP_IS_VALID] =
        g_param_spec_boolean ("is-valid",
                              "Is Valid",
                              "Whether current placement is valid",
                              FALSE,
                              G_PARAM_READABLE |
                              G_PARAM_STATIC_STRINGS);

    g_object_class_install_properties (object_class, N_PROPS, properties);

    /**
     * LrgPlacementSystem::placement-started:
     * @system: the placement system
     * @definition: the building definition
     *
     * Emitted when placement mode starts.
     *
     * Since: 1.0
     */
    signals[SIGNAL_PLACEMENT_STARTED] =
        g_signal_new ("placement-started",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0,
                      NULL, NULL,
                      NULL,
                      G_TYPE_NONE, 1,
                      LRG_TYPE_BUILDING_DEF);

    /**
     * LrgPlacementSystem::placement-cancelled:
     * @system: the placement system
     *
     * Emitted when placement is cancelled.
     *
     * Since: 1.0
     */
    signals[SIGNAL_PLACEMENT_CANCELLED] =
        g_signal_new ("placement-cancelled",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0,
                      NULL, NULL,
                      NULL,
                      G_TYPE_NONE, 0);

    /**
     * LrgPlacementSystem::placement-confirmed:
     * @system: the placement system
     * @building: the placed building
     *
     * Emitted when a building is placed.
     *
     * Since: 1.0
     */
    signals[SIGNAL_PLACEMENT_CONFIRMED] =
        g_signal_new ("placement-confirmed",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0,
                      NULL, NULL,
                      NULL,
                      G_TYPE_NONE, 1,
                      LRG_TYPE_BUILDING_INSTANCE);

    /**
     * LrgPlacementSystem::building-demolished:
     * @system: the placement system
     * @building: the demolished building
     *
     * Emitted when a building is demolished.
     *
     * Since: 1.0
     */
    signals[SIGNAL_BUILDING_DEMOLISHED] =
        g_signal_new ("building-demolished",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0,
                      NULL, NULL,
                      NULL,
                      G_TYPE_NONE, 1,
                      LRG_TYPE_BUILDING_INSTANCE);

    /**
     * LrgPlacementSystem::validity-changed:
     * @system: the placement system
     * @is_valid: whether placement is now valid
     *
     * Emitted when placement validity changes.
     *
     * Since: 1.0
     */
    signals[SIGNAL_VALIDITY_CHANGED] =
        g_signal_new ("validity-changed",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0,
                      NULL, NULL,
                      NULL,
                      G_TYPE_NONE, 1,
                      G_TYPE_BOOLEAN);
}

static void
lrg_placement_system_init (LrgPlacementSystem *self)
{
    self->state = LRG_PLACEMENT_STATE_IDLE;
    self->rotation = LRG_ROTATION_0;
    self->is_valid = FALSE;
}

/* Public API */

LrgPlacementSystem *
lrg_placement_system_new (LrgBuildGrid *grid)
{
    g_return_val_if_fail (grid == NULL || LRG_IS_BUILD_GRID (grid), NULL);

    return g_object_new (LRG_TYPE_PLACEMENT_SYSTEM,
                         "grid", grid,
                         NULL);
}

LrgBuildGrid *
lrg_placement_system_get_grid (LrgPlacementSystem *self)
{
    g_return_val_if_fail (LRG_IS_PLACEMENT_SYSTEM (self), NULL);

    return self->grid;
}

void
lrg_placement_system_set_grid (LrgPlacementSystem *self,
                               LrgBuildGrid       *grid)
{
    g_return_if_fail (LRG_IS_PLACEMENT_SYSTEM (self));
    g_return_if_fail (grid == NULL || LRG_IS_BUILD_GRID (grid));

    if (g_set_object (&self->grid, grid))
    {
        /* Cancel any active placement when grid changes */
        if (self->state != LRG_PLACEMENT_STATE_IDLE)
        {
            lrg_placement_system_cancel (self);
        }

        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_GRID]);
    }
}

LrgPlacementState
lrg_placement_system_get_state (LrgPlacementSystem *self)
{
    g_return_val_if_fail (LRG_IS_PLACEMENT_SYSTEM (self), LRG_PLACEMENT_STATE_IDLE);

    return self->state;
}

gboolean
lrg_placement_system_is_placing (LrgPlacementSystem *self)
{
    g_return_val_if_fail (LRG_IS_PLACEMENT_SYSTEM (self), FALSE);

    return self->state == LRG_PLACEMENT_STATE_PLACING;
}

gboolean
lrg_placement_system_is_demolishing (LrgPlacementSystem *self)
{
    g_return_val_if_fail (LRG_IS_PLACEMENT_SYSTEM (self), FALSE);

    return self->state == LRG_PLACEMENT_STATE_DEMOLISHING;
}

gboolean
lrg_placement_system_start_placement (LrgPlacementSystem *self,
                                      LrgBuildingDef     *definition)
{
    g_return_val_if_fail (LRG_IS_PLACEMENT_SYSTEM (self), FALSE);
    g_return_val_if_fail (LRG_IS_BUILDING_DEF (definition), FALSE);
    g_return_val_if_fail (self->grid != NULL, FALSE);

    /* Cancel any existing placement */
    if (self->state != LRG_PLACEMENT_STATE_IDLE)
    {
        lrg_placement_system_cancel (self);
    }

    g_set_object (&self->current_def, definition);
    self->state = LRG_PLACEMENT_STATE_PLACING;
    self->rotation = LRG_ROTATION_0;
    self->grid_x = 0;
    self->grid_y = 0;

    update_validity (self);

    lrg_debug (LRG_LOG_DOMAIN_BUILDING,
               "Started placement of '%s'",
               lrg_building_def_get_id (definition));

    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_STATE]);
    g_signal_emit (self, signals[SIGNAL_PLACEMENT_STARTED], 0, definition);

    return TRUE;
}

void
lrg_placement_system_cancel (LrgPlacementSystem *self)
{
    g_return_if_fail (LRG_IS_PLACEMENT_SYSTEM (self));

    if (self->state == LRG_PLACEMENT_STATE_IDLE)
        return;

    self->state = LRG_PLACEMENT_STATE_IDLE;
    g_clear_object (&self->current_def);
    self->is_valid = FALSE;

    lrg_debug (LRG_LOG_DOMAIN_BUILDING, "Cancelled placement");

    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_STATE]);
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_IS_VALID]);
    g_signal_emit (self, signals[SIGNAL_PLACEMENT_CANCELLED], 0);
}

void
lrg_placement_system_update_position (LrgPlacementSystem *self,
                                      gdouble             world_x,
                                      gdouble             world_y)
{
    gint cell_x;
    gint cell_y;

    g_return_if_fail (LRG_IS_PLACEMENT_SYSTEM (self));

    if (self->grid == NULL)
        return;

    lrg_build_grid_world_to_cell (self->grid, world_x, world_y, &cell_x, &cell_y);
    lrg_placement_system_set_grid_position (self, cell_x, cell_y);
}

void
lrg_placement_system_set_grid_position (LrgPlacementSystem *self,
                                        gint                grid_x,
                                        gint                grid_y)
{
    g_return_if_fail (LRG_IS_PLACEMENT_SYSTEM (self));

    if (self->grid_x == grid_x && self->grid_y == grid_y)
        return;

    self->grid_x = grid_x;
    self->grid_y = grid_y;

    update_validity (self);
}

void
lrg_placement_system_get_grid_position (LrgPlacementSystem *self,
                                        gint               *grid_x,
                                        gint               *grid_y)
{
    g_return_if_fail (LRG_IS_PLACEMENT_SYSTEM (self));

    if (grid_x != NULL)
        *grid_x = self->grid_x;
    if (grid_y != NULL)
        *grid_y = self->grid_y;
}

void
lrg_placement_system_rotate_cw (LrgPlacementSystem *self)
{
    g_return_if_fail (LRG_IS_PLACEMENT_SYSTEM (self));

    switch (self->rotation)
    {
    case LRG_ROTATION_0:
        self->rotation = LRG_ROTATION_90;
        break;
    case LRG_ROTATION_90:
        self->rotation = LRG_ROTATION_180;
        break;
    case LRG_ROTATION_180:
        self->rotation = LRG_ROTATION_270;
        break;
    case LRG_ROTATION_270:
        self->rotation = LRG_ROTATION_0;
        break;
    }

    update_validity (self);
}

void
lrg_placement_system_rotate_ccw (LrgPlacementSystem *self)
{
    g_return_if_fail (LRG_IS_PLACEMENT_SYSTEM (self));

    switch (self->rotation)
    {
    case LRG_ROTATION_0:
        self->rotation = LRG_ROTATION_270;
        break;
    case LRG_ROTATION_90:
        self->rotation = LRG_ROTATION_0;
        break;
    case LRG_ROTATION_180:
        self->rotation = LRG_ROTATION_90;
        break;
    case LRG_ROTATION_270:
        self->rotation = LRG_ROTATION_180;
        break;
    }

    update_validity (self);
}

LrgRotation
lrg_placement_system_get_rotation (LrgPlacementSystem *self)
{
    g_return_val_if_fail (LRG_IS_PLACEMENT_SYSTEM (self), LRG_ROTATION_0);

    return self->rotation;
}

void
lrg_placement_system_set_rotation (LrgPlacementSystem *self,
                                   LrgRotation         rotation)
{
    g_return_if_fail (LRG_IS_PLACEMENT_SYSTEM (self));

    if (self->rotation != rotation)
    {
        self->rotation = rotation;
        update_validity (self);
    }
}

gboolean
lrg_placement_system_is_valid (LrgPlacementSystem *self)
{
    g_return_val_if_fail (LRG_IS_PLACEMENT_SYSTEM (self), FALSE);

    return self->is_valid;
}

LrgBuildingDef *
lrg_placement_system_get_current_definition (LrgPlacementSystem *self)
{
    g_return_val_if_fail (LRG_IS_PLACEMENT_SYSTEM (self), NULL);

    return self->current_def;
}

LrgBuildingInstance *
lrg_placement_system_confirm (LrgPlacementSystem *self)
{
    LrgBuildingInstance *building;
    gboolean             placed;

    g_return_val_if_fail (LRG_IS_PLACEMENT_SYSTEM (self), NULL);

    if (self->state != LRG_PLACEMENT_STATE_PLACING)
    {
        lrg_warning (LRG_LOG_DOMAIN_BUILDING, "Not in placement mode");
        return NULL;
    }

    if (!self->is_valid)
    {
        lrg_warning (LRG_LOG_DOMAIN_BUILDING, "Current placement is not valid");
        return NULL;
    }

    /* Create the building instance */
    building = lrg_building_instance_new (self->current_def,
                                          self->grid_x,
                                          self->grid_y);
    lrg_building_instance_set_rotation (building, self->rotation);

    /* Place on grid */
    placed = lrg_build_grid_place_building (self->grid, building);
    if (!placed)
    {
        g_object_unref (building);
        lrg_warning (LRG_LOG_DOMAIN_BUILDING, "Failed to place building on grid");
        return NULL;
    }

    lrg_debug (LRG_LOG_DOMAIN_BUILDING,
               "Confirmed placement of '%s' at (%d, %d)",
               lrg_building_def_get_id (self->current_def),
               self->grid_x, self->grid_y);

    g_signal_emit (self, signals[SIGNAL_PLACEMENT_CONFIRMED], 0, building);

    /* Exit placement mode */
    self->state = LRG_PLACEMENT_STATE_IDLE;
    g_clear_object (&self->current_def);
    self->is_valid = FALSE;

    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_STATE]);
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_IS_VALID]);

    return building;
}

LrgBuildingInstance *
lrg_placement_system_confirm_and_continue (LrgPlacementSystem *self)
{
    g_autoptr(LrgBuildingDef) def = NULL;
    LrgBuildingInstance      *building;
    gboolean                  placed;

    g_return_val_if_fail (LRG_IS_PLACEMENT_SYSTEM (self), NULL);

    if (self->state != LRG_PLACEMENT_STATE_PLACING)
    {
        lrg_warning (LRG_LOG_DOMAIN_BUILDING, "Not in placement mode");
        return NULL;
    }

    if (!self->is_valid)
    {
        lrg_warning (LRG_LOG_DOMAIN_BUILDING, "Current placement is not valid");
        return NULL;
    }

    /* Create the building instance */
    building = lrg_building_instance_new (self->current_def,
                                          self->grid_x,
                                          self->grid_y);
    lrg_building_instance_set_rotation (building, self->rotation);

    /* Place on grid */
    placed = lrg_build_grid_place_building (self->grid, building);
    if (!placed)
    {
        g_object_unref (building);
        lrg_warning (LRG_LOG_DOMAIN_BUILDING, "Failed to place building on grid");
        return NULL;
    }

    lrg_debug (LRG_LOG_DOMAIN_BUILDING,
               "Confirmed placement of '%s' at (%d, %d), continuing",
               lrg_building_def_get_id (self->current_def),
               self->grid_x, self->grid_y);

    g_signal_emit (self, signals[SIGNAL_PLACEMENT_CONFIRMED], 0, building);

    /* Stay in placement mode, revalidate at current position */
    update_validity (self);

    return building;
}

void
lrg_placement_system_start_demolition (LrgPlacementSystem *self)
{
    g_return_if_fail (LRG_IS_PLACEMENT_SYSTEM (self));

    /* Cancel any active placement */
    if (self->state == LRG_PLACEMENT_STATE_PLACING)
    {
        lrg_placement_system_cancel (self);
    }

    self->state = LRG_PLACEMENT_STATE_DEMOLISHING;
    g_clear_object (&self->current_def);
    self->is_valid = FALSE;

    lrg_debug (LRG_LOG_DOMAIN_BUILDING, "Entered demolition mode");

    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_STATE]);
}

LrgBuildingInstance *
lrg_placement_system_demolish_at (LrgPlacementSystem *self,
                                  gint                grid_x,
                                  gint                grid_y)
{
    LrgBuildingInstance *building;

    g_return_val_if_fail (LRG_IS_PLACEMENT_SYSTEM (self), NULL);
    g_return_val_if_fail (self->grid != NULL, NULL);

    building = lrg_build_grid_get_building_at (self->grid, grid_x, grid_y);
    if (building == NULL)
    {
        lrg_debug (LRG_LOG_DOMAIN_BUILDING,
                   "No building at (%d, %d) to demolish", grid_x, grid_y);
        return NULL;
    }

    /* Keep a reference during removal */
    g_object_ref (building);

    if (!lrg_build_grid_remove_building (self->grid, building))
    {
        g_object_unref (building);
        return NULL;
    }

    lrg_debug (LRG_LOG_DOMAIN_BUILDING,
               "Demolished building at (%d, %d)", grid_x, grid_y);

    g_signal_emit (self, signals[SIGNAL_BUILDING_DEMOLISHED], 0, building);

    /* We still return the building (caller may want to refund resources) */
    g_object_unref (building);
    return building;
}

LrgBuildingInstance *
lrg_placement_system_get_building_at_cursor (LrgPlacementSystem *self)
{
    g_return_val_if_fail (LRG_IS_PLACEMENT_SYSTEM (self), NULL);

    if (self->grid == NULL)
        return NULL;

    return lrg_build_grid_get_building_at (self->grid, self->grid_x, self->grid_y);
}

void
lrg_placement_system_set_resource_check (LrgPlacementSystem        *self,
                                         LrgPlacementResourceCheck  check,
                                         gpointer                   user_data,
                                         GDestroyNotify             destroy)
{
    g_return_if_fail (LRG_IS_PLACEMENT_SYSTEM (self));

    /* Clean up old callback */
    if (self->resource_check_destroy != NULL && self->resource_check_data != NULL)
    {
        self->resource_check_destroy (self->resource_check_data);
    }

    self->resource_check = check;
    self->resource_check_data = user_data;
    self->resource_check_destroy = destroy;

    /* Revalidate if currently placing */
    if (self->state == LRG_PLACEMENT_STATE_PLACING)
    {
        update_validity (self);
    }
}
