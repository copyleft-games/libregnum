/* lrg-nav-cell.c
 *
 * Copyright 2024 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Navigation cell implementation.
 */

#include "lrg-nav-cell.h"

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_PATHFIND
#include "lrg-log.h"

struct _LrgNavCell
{
    gint             x;
    gint             y;
    gfloat           cost;
    LrgNavCellFlags  flags;
    gpointer         user_data;
    GDestroyNotify   user_data_destroy;
};

G_DEFINE_BOXED_TYPE (LrgNavCell, lrg_nav_cell,
                     lrg_nav_cell_copy,
                     lrg_nav_cell_free)

/**
 * lrg_nav_cell_new:
 * @x: X coordinate in the grid
 * @y: Y coordinate in the grid
 * @cost: Movement cost (1.0 = normal, higher = slower)
 * @flags: Navigation flags for this cell
 *
 * Creates a new navigation cell with the specified properties.
 *
 * Returns: (transfer full): A new #LrgNavCell
 */
LrgNavCell *
lrg_nav_cell_new (gint            x,
                  gint            y,
                  gfloat          cost,
                  LrgNavCellFlags flags)
{
    LrgNavCell *self;

    self = g_slice_new0 (LrgNavCell);
    self->x = x;
    self->y = y;
    self->cost = cost;
    self->flags = flags;
    self->user_data = NULL;
    self->user_data_destroy = NULL;

    return self;
}

/**
 * lrg_nav_cell_copy:
 * @self: an #LrgNavCell
 *
 * Creates a deep copy of a navigation cell.
 * Note: user_data is NOT copied (set to NULL in copy).
 *
 * Returns: (transfer full): A copy of @self
 */
LrgNavCell *
lrg_nav_cell_copy (const LrgNavCell *self)
{
    LrgNavCell *copy;

    g_return_val_if_fail (self != NULL, NULL);

    copy = g_slice_new0 (LrgNavCell);
    copy->x = self->x;
    copy->y = self->y;
    copy->cost = self->cost;
    copy->flags = self->flags;
    /* user_data is not copied */
    copy->user_data = NULL;
    copy->user_data_destroy = NULL;

    return copy;
}

/**
 * lrg_nav_cell_free:
 * @self: an #LrgNavCell
 *
 * Frees a navigation cell.
 */
void
lrg_nav_cell_free (LrgNavCell *self)
{
    if (self == NULL)
        return;

    if (self->user_data != NULL && self->user_data_destroy != NULL)
        self->user_data_destroy (self->user_data);

    g_slice_free (LrgNavCell, self);
}

/**
 * lrg_nav_cell_get_x:
 * @self: an #LrgNavCell
 *
 * Gets the X coordinate.
 *
 * Returns: The X coordinate
 */
gint
lrg_nav_cell_get_x (const LrgNavCell *self)
{
    g_return_val_if_fail (self != NULL, 0);
    return self->x;
}

/**
 * lrg_nav_cell_get_y:
 * @self: an #LrgNavCell
 *
 * Gets the Y coordinate.
 *
 * Returns: The Y coordinate
 */
gint
lrg_nav_cell_get_y (const LrgNavCell *self)
{
    g_return_val_if_fail (self != NULL, 0);
    return self->y;
}

/**
 * lrg_nav_cell_get_cost:
 * @self: an #LrgNavCell
 *
 * Gets the movement cost multiplier.
 *
 * Returns: The movement cost
 */
gfloat
lrg_nav_cell_get_cost (const LrgNavCell *self)
{
    g_return_val_if_fail (self != NULL, 1.0f);
    return self->cost;
}

/**
 * lrg_nav_cell_set_cost:
 * @self: an #LrgNavCell
 * @cost: New movement cost
 *
 * Sets the movement cost multiplier.
 */
void
lrg_nav_cell_set_cost (LrgNavCell *self,
                       gfloat      cost)
{
    g_return_if_fail (self != NULL);
    g_return_if_fail (cost >= 0.0f);

    self->cost = cost;
}

/**
 * lrg_nav_cell_get_flags:
 * @self: an #LrgNavCell
 *
 * Gets the navigation flags.
 *
 * Returns: The navigation flags
 */
LrgNavCellFlags
lrg_nav_cell_get_flags (const LrgNavCell *self)
{
    g_return_val_if_fail (self != NULL, LRG_NAV_CELL_NONE);
    return self->flags;
}

/**
 * lrg_nav_cell_set_flags:
 * @self: an #LrgNavCell
 * @flags: New navigation flags
 *
 * Sets the navigation flags.
 */
void
lrg_nav_cell_set_flags (LrgNavCell      *self,
                        LrgNavCellFlags  flags)
{
    g_return_if_fail (self != NULL);
    self->flags = flags;
}

/**
 * lrg_nav_cell_has_flag:
 * @self: an #LrgNavCell
 * @flag: Flag to check
 *
 * Checks if the cell has a specific flag set.
 *
 * Returns: %TRUE if the flag is set
 */
gboolean
lrg_nav_cell_has_flag (const LrgNavCell *self,
                       LrgNavCellFlags   flag)
{
    g_return_val_if_fail (self != NULL, FALSE);
    return (self->flags & flag) != 0;
}

/**
 * lrg_nav_cell_is_walkable:
 * @self: an #LrgNavCell
 *
 * Checks if the cell is walkable (does not have BLOCKED flag).
 *
 * Returns: %TRUE if the cell is walkable
 */
gboolean
lrg_nav_cell_is_walkable (const LrgNavCell *self)
{
    g_return_val_if_fail (self != NULL, FALSE);
    return (self->flags & LRG_NAV_CELL_BLOCKED) == 0;
}

/**
 * lrg_nav_cell_get_user_data:
 * @self: an #LrgNavCell
 *
 * Gets user-defined data attached to this cell.
 *
 * Returns: (transfer none) (nullable): User data or %NULL
 */
gpointer
lrg_nav_cell_get_user_data (const LrgNavCell *self)
{
    g_return_val_if_fail (self != NULL, NULL);
    return self->user_data;
}

/**
 * lrg_nav_cell_set_user_data:
 * @self: an #LrgNavCell
 * @user_data: (nullable): User data to attach
 * @destroy: (nullable): Destroy function for user data
 *
 * Sets user-defined data on this cell. Any existing user data will be
 * freed using its destroy function.
 */
void
lrg_nav_cell_set_user_data (LrgNavCell     *self,
                            gpointer        user_data,
                            GDestroyNotify  destroy)
{
    g_return_if_fail (self != NULL);

    /* Free existing user data */
    if (self->user_data != NULL && self->user_data_destroy != NULL)
        self->user_data_destroy (self->user_data);

    self->user_data = user_data;
    self->user_data_destroy = destroy;
}
