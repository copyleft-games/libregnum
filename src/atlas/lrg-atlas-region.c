/* lrg-atlas-region.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Texture atlas region implementation.
 */

#include "config.h"

#include "lrg-atlas-region.h"

/**
 * LrgAtlasRegion:
 *
 * A region within a texture atlas.
 */
struct _LrgAtlasRegion
{
    /* Identity */
    gchar  *name;

    /* Position and size in pixels */
    gint    x;
    gint    y;
    gint    width;
    gint    height;

    /* UV coordinates (0.0-1.0) */
    gfloat  u1;
    gfloat  v1;
    gfloat  u2;
    gfloat  v2;

    /* Transform flags */
    gboolean rotated;
    gboolean flipped_x;
    gboolean flipped_y;

    /* Pivot point for positioning */
    gfloat  pivot_x;
    gfloat  pivot_y;
};

G_DEFINE_BOXED_TYPE (LrgAtlasRegion,
                     lrg_atlas_region,
                     lrg_atlas_region_copy,
                     lrg_atlas_region_free)

/**
 * lrg_atlas_region_new:
 * @name: Region name/identifier
 * @x: X position in the atlas (pixels)
 * @y: Y position in the atlas (pixels)
 * @width: Width of the region (pixels)
 * @height: Height of the region (pixels)
 *
 * Creates a new atlas region.
 *
 * Returns: (transfer full): A new #LrgAtlasRegion
 *
 * Since: 1.0
 */
LrgAtlasRegion *
lrg_atlas_region_new (const gchar *name,
                      gint         x,
                      gint         y,
                      gint         width,
                      gint         height)
{
    LrgAtlasRegion *self;

    self = g_new0 (LrgAtlasRegion, 1);
    self->name      = g_strdup (name);
    self->x         = x;
    self->y         = y;
    self->width     = width;
    self->height    = height;
    self->u1        = 0.0f;
    self->v1        = 0.0f;
    self->u2        = 1.0f;
    self->v2        = 1.0f;
    self->rotated   = FALSE;
    self->flipped_x = FALSE;
    self->flipped_y = FALSE;
    self->pivot_x   = 0.0f;
    self->pivot_y   = 0.0f;

    return self;
}

/**
 * lrg_atlas_region_new_with_uv:
 * @name: Region name/identifier
 * @x: X position in the atlas (pixels)
 * @y: Y position in the atlas (pixels)
 * @width: Width of the region (pixels)
 * @height: Height of the region (pixels)
 * @u1: Left UV coordinate (0.0-1.0)
 * @v1: Top UV coordinate (0.0-1.0)
 * @u2: Right UV coordinate (0.0-1.0)
 * @v2: Bottom UV coordinate (0.0-1.0)
 *
 * Creates a new atlas region with explicit UV coordinates.
 *
 * Returns: (transfer full): A new #LrgAtlasRegion
 *
 * Since: 1.0
 */
LrgAtlasRegion *
lrg_atlas_region_new_with_uv (const gchar *name,
                              gint         x,
                              gint         y,
                              gint         width,
                              gint         height,
                              gfloat       u1,
                              gfloat       v1,
                              gfloat       u2,
                              gfloat       v2)
{
    LrgAtlasRegion *self;

    self = lrg_atlas_region_new (name, x, y, width, height);
    self->u1 = u1;
    self->v1 = v1;
    self->u2 = u2;
    self->v2 = v2;

    return self;
}

/**
 * lrg_atlas_region_copy:
 * @self: A #LrgAtlasRegion
 *
 * Creates a copy of an atlas region.
 *
 * Returns: (transfer full): A copy of the region
 *
 * Since: 1.0
 */
LrgAtlasRegion *
lrg_atlas_region_copy (const LrgAtlasRegion *self)
{
    LrgAtlasRegion *copy;

    g_return_val_if_fail (self != NULL, NULL);

    copy = g_new0 (LrgAtlasRegion, 1);
    copy->name      = g_strdup (self->name);
    copy->x         = self->x;
    copy->y         = self->y;
    copy->width     = self->width;
    copy->height    = self->height;
    copy->u1        = self->u1;
    copy->v1        = self->v1;
    copy->u2        = self->u2;
    copy->v2        = self->v2;
    copy->rotated   = self->rotated;
    copy->flipped_x = self->flipped_x;
    copy->flipped_y = self->flipped_y;
    copy->pivot_x   = self->pivot_x;
    copy->pivot_y   = self->pivot_y;

    return copy;
}

/**
 * lrg_atlas_region_free:
 * @self: A #LrgAtlasRegion
 *
 * Frees an atlas region.
 *
 * Since: 1.0
 */
void
lrg_atlas_region_free (LrgAtlasRegion *self)
{
    if (self != NULL)
    {
        g_free (self->name);
        g_free (self);
    }
}

/**
 * lrg_atlas_region_get_name:
 * @self: A #LrgAtlasRegion
 *
 * Gets the name of the region.
 *
 * Returns: (transfer none) (nullable): The region name
 *
 * Since: 1.0
 */
const gchar *
lrg_atlas_region_get_name (const LrgAtlasRegion *self)
{
    g_return_val_if_fail (self != NULL, NULL);
    return self->name;
}

/**
 * lrg_atlas_region_get_x:
 * @self: A #LrgAtlasRegion
 *
 * Gets the X position in the atlas.
 *
 * Returns: X position in pixels
 *
 * Since: 1.0
 */
gint
lrg_atlas_region_get_x (const LrgAtlasRegion *self)
{
    g_return_val_if_fail (self != NULL, 0);
    return self->x;
}

/**
 * lrg_atlas_region_get_y:
 * @self: A #LrgAtlasRegion
 *
 * Gets the Y position in the atlas.
 *
 * Returns: Y position in pixels
 *
 * Since: 1.0
 */
gint
lrg_atlas_region_get_y (const LrgAtlasRegion *self)
{
    g_return_val_if_fail (self != NULL, 0);
    return self->y;
}

/**
 * lrg_atlas_region_get_width:
 * @self: A #LrgAtlasRegion
 *
 * Gets the width of the region.
 *
 * Returns: Width in pixels
 *
 * Since: 1.0
 */
gint
lrg_atlas_region_get_width (const LrgAtlasRegion *self)
{
    g_return_val_if_fail (self != NULL, 0);
    return self->width;
}

/**
 * lrg_atlas_region_get_height:
 * @self: A #LrgAtlasRegion
 *
 * Gets the height of the region.
 *
 * Returns: Height in pixels
 *
 * Since: 1.0
 */
gint
lrg_atlas_region_get_height (const LrgAtlasRegion *self)
{
    g_return_val_if_fail (self != NULL, 0);
    return self->height;
}

/**
 * lrg_atlas_region_get_rect:
 * @self: A #LrgAtlasRegion
 * @out_x: (out) (nullable): Return location for X
 * @out_y: (out) (nullable): Return location for Y
 * @out_width: (out) (nullable): Return location for width
 * @out_height: (out) (nullable): Return location for height
 *
 * Gets the full rectangle of the region.
 *
 * Since: 1.0
 */
void
lrg_atlas_region_get_rect (const LrgAtlasRegion *self,
                           gint                 *out_x,
                           gint                 *out_y,
                           gint                 *out_width,
                           gint                 *out_height)
{
    g_return_if_fail (self != NULL);

    if (out_x != NULL)
        *out_x = self->x;
    if (out_y != NULL)
        *out_y = self->y;
    if (out_width != NULL)
        *out_width = self->width;
    if (out_height != NULL)
        *out_height = self->height;
}

/**
 * lrg_atlas_region_get_u1:
 * @self: A #LrgAtlasRegion
 *
 * Gets the left UV coordinate.
 *
 * Returns: U1 coordinate (0.0-1.0)
 *
 * Since: 1.0
 */
gfloat
lrg_atlas_region_get_u1 (const LrgAtlasRegion *self)
{
    g_return_val_if_fail (self != NULL, 0.0f);
    return self->u1;
}

/**
 * lrg_atlas_region_get_v1:
 * @self: A #LrgAtlasRegion
 *
 * Gets the top UV coordinate.
 *
 * Returns: V1 coordinate (0.0-1.0)
 *
 * Since: 1.0
 */
gfloat
lrg_atlas_region_get_v1 (const LrgAtlasRegion *self)
{
    g_return_val_if_fail (self != NULL, 0.0f);
    return self->v1;
}

/**
 * lrg_atlas_region_get_u2:
 * @self: A #LrgAtlasRegion
 *
 * Gets the right UV coordinate.
 *
 * Returns: U2 coordinate (0.0-1.0)
 *
 * Since: 1.0
 */
gfloat
lrg_atlas_region_get_u2 (const LrgAtlasRegion *self)
{
    g_return_val_if_fail (self != NULL, 1.0f);
    return self->u2;
}

/**
 * lrg_atlas_region_get_v2:
 * @self: A #LrgAtlasRegion
 *
 * Gets the bottom UV coordinate.
 *
 * Returns: V2 coordinate (0.0-1.0)
 *
 * Since: 1.0
 */
gfloat
lrg_atlas_region_get_v2 (const LrgAtlasRegion *self)
{
    g_return_val_if_fail (self != NULL, 1.0f);
    return self->v2;
}

/**
 * lrg_atlas_region_get_uv:
 * @self: A #LrgAtlasRegion
 * @out_u1: (out) (nullable): Return location for U1
 * @out_v1: (out) (nullable): Return location for V1
 * @out_u2: (out) (nullable): Return location for U2
 * @out_v2: (out) (nullable): Return location for V2
 *
 * Gets all UV coordinates.
 *
 * Since: 1.0
 */
void
lrg_atlas_region_get_uv (const LrgAtlasRegion *self,
                         gfloat               *out_u1,
                         gfloat               *out_v1,
                         gfloat               *out_u2,
                         gfloat               *out_v2)
{
    g_return_if_fail (self != NULL);

    if (out_u1 != NULL)
        *out_u1 = self->u1;
    if (out_v1 != NULL)
        *out_v1 = self->v1;
    if (out_u2 != NULL)
        *out_u2 = self->u2;
    if (out_v2 != NULL)
        *out_v2 = self->v2;
}

/**
 * lrg_atlas_region_is_rotated:
 * @self: A #LrgAtlasRegion
 *
 * Checks if the region is rotated 90 degrees.
 *
 * Returns: %TRUE if rotated
 *
 * Since: 1.0
 */
gboolean
lrg_atlas_region_is_rotated (const LrgAtlasRegion *self)
{
    g_return_val_if_fail (self != NULL, FALSE);
    return self->rotated;
}

/**
 * lrg_atlas_region_set_rotated:
 * @self: A #LrgAtlasRegion
 * @rotated: Whether the region is rotated
 *
 * Sets whether the region is rotated.
 *
 * Since: 1.0
 */
void
lrg_atlas_region_set_rotated (LrgAtlasRegion *self,
                              gboolean        rotated)
{
    g_return_if_fail (self != NULL);
    self->rotated = rotated;
}

/**
 * lrg_atlas_region_is_flipped_x:
 * @self: A #LrgAtlasRegion
 *
 * Checks if the region is flipped horizontally.
 *
 * Returns: %TRUE if flipped on X axis
 *
 * Since: 1.0
 */
gboolean
lrg_atlas_region_is_flipped_x (const LrgAtlasRegion *self)
{
    g_return_val_if_fail (self != NULL, FALSE);
    return self->flipped_x;
}

/**
 * lrg_atlas_region_set_flipped_x:
 * @self: A #LrgAtlasRegion
 * @flipped: Whether to flip horizontally
 *
 * Sets horizontal flip.
 *
 * Since: 1.0
 */
void
lrg_atlas_region_set_flipped_x (LrgAtlasRegion *self,
                                gboolean        flipped)
{
    g_return_if_fail (self != NULL);
    self->flipped_x = flipped;
}

/**
 * lrg_atlas_region_is_flipped_y:
 * @self: A #LrgAtlasRegion
 *
 * Checks if the region is flipped vertically.
 *
 * Returns: %TRUE if flipped on Y axis
 *
 * Since: 1.0
 */
gboolean
lrg_atlas_region_is_flipped_y (const LrgAtlasRegion *self)
{
    g_return_val_if_fail (self != NULL, FALSE);
    return self->flipped_y;
}

/**
 * lrg_atlas_region_set_flipped_y:
 * @self: A #LrgAtlasRegion
 * @flipped: Whether to flip vertically
 *
 * Sets vertical flip.
 *
 * Since: 1.0
 */
void
lrg_atlas_region_set_flipped_y (LrgAtlasRegion *self,
                                gboolean        flipped)
{
    g_return_if_fail (self != NULL);
    self->flipped_y = flipped;
}

/**
 * lrg_atlas_region_get_pivot_x:
 * @self: A #LrgAtlasRegion
 *
 * Gets the pivot X offset.
 *
 * Returns: Pivot X in pixels
 *
 * Since: 1.0
 */
gfloat
lrg_atlas_region_get_pivot_x (const LrgAtlasRegion *self)
{
    g_return_val_if_fail (self != NULL, 0.0f);
    return self->pivot_x;
}

/**
 * lrg_atlas_region_get_pivot_y:
 * @self: A #LrgAtlasRegion
 *
 * Gets the pivot Y offset.
 *
 * Returns: Pivot Y in pixels
 *
 * Since: 1.0
 */
gfloat
lrg_atlas_region_get_pivot_y (const LrgAtlasRegion *self)
{
    g_return_val_if_fail (self != NULL, 0.0f);
    return self->pivot_y;
}

/**
 * lrg_atlas_region_set_pivot:
 * @self: A #LrgAtlasRegion
 * @x: Pivot X offset
 * @y: Pivot Y offset
 *
 * Sets the pivot point.
 *
 * Since: 1.0
 */
void
lrg_atlas_region_set_pivot (LrgAtlasRegion *self,
                            gfloat          x,
                            gfloat          y)
{
    g_return_if_fail (self != NULL);
    self->pivot_x = x;
    self->pivot_y = y;
}

/**
 * lrg_atlas_region_calculate_uv:
 * @self: A #LrgAtlasRegion
 * @atlas_width: Total width of the atlas texture
 * @atlas_height: Total height of the atlas texture
 *
 * Calculates and updates UV coordinates based on pixel position
 * and atlas dimensions.
 *
 * Since: 1.0
 */
void
lrg_atlas_region_calculate_uv (LrgAtlasRegion *self,
                               gint            atlas_width,
                               gint            atlas_height)
{
    g_return_if_fail (self != NULL);
    g_return_if_fail (atlas_width > 0);
    g_return_if_fail (atlas_height > 0);

    self->u1 = (gfloat)self->x / (gfloat)atlas_width;
    self->v1 = (gfloat)self->y / (gfloat)atlas_height;
    self->u2 = (gfloat)(self->x + self->width) / (gfloat)atlas_width;
    self->v2 = (gfloat)(self->y + self->height) / (gfloat)atlas_height;
}
